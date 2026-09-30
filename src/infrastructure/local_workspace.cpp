#include "muz/infrastructure/local_workspace.hpp"
#include "muz/application/import_batch.hpp"
#include "local_files.hpp"
#include "qt_paths.hpp"
#include "sqlite_repository.hpp"
#include "accounting_adapters.hpp"
#include "response_adapters.hpp"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QUuid>
#include <mutex>
#include <QCryptographicHash>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QFile>
#include <QDate>
#include <algorithm>
#include <set>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace muz {
namespace {
class SystemIdentity final : public IdentityClock {
public:
    std::string new_id() override { return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(); }
    std::string now_utc() override { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString(); }
};

void apply_temporary_service_date(AccountingRow& row, const std::string& date) {
    // TODO(MUZ-5): replace the temporary local date with the service date from KEP.
    row.cells[service_date] = date;
    row.approved = false;
    std::erase(row.warnings, "Tebliğ tarihi belgede yok; kullanıcı girişi gerekli");
    row.evidence.push_back({service_date, {}, date, "temporary-local-date-until-kep", 0.0});
}

std::vector<AccountingRow> load_accounting(SqliteRepository& repository, const std::string& batch_id) {
    auto rows = repository.accounting_rows(batch_id);
    std::vector<AccountingRow> expanded;
    bool changed=false;
    for(const auto& row:rows) {
        auto split=split_legacy_debtors(row);
        changed=changed || split.size()!=1 || encode_row(split.front())!=encode_row(row);
        expanded.insert(expanded.end(),std::make_move_iterator(split.begin()),std::make_move_iterator(split.end()));
    }
    if(changed) { repository.replace_accounting(batch_id,expanded); rows=std::move(expanded); }
    std::vector<AccountingRow> updated;
    const auto today = QDate::currentDate().toString("dd.MM.yyyy").toStdString();
    for (auto& row : rows) {
        // Older batches predate the temporary default. Preserve deliberate manual
        // edits (including an explicitly cleared date) and all nonempty dates.
        const bool date_reviewed = std::any_of(row.evidence.begin(), row.evidence.end(), [](const auto& field) {
            return field.column == service_date && field.method == "manual-review";
        });
        if (row.cells[service_date].empty() && !date_reviewed) {
            apply_temporary_service_date(row, today);
            updated.push_back(row);
        }
    }
    if (!updated.empty()) repository.save_accounting(batch_id, updated, true);
    return rows;
}
} // namespace

struct LocalWorkspace::Impl {
    std::filesystem::path root;
    std::unique_ptr<QLockFile> lock;
    std::mutex writer;
};

LocalWorkspace::LocalWorkspace(std::filesystem::path root) : impl_(std::make_unique<Impl>()) {
    if (!QDir().mkpath(qpath(root))) throw Error(ErrorCode::file_io);
    impl_->root = native_path(QFileInfo(qpath(root)).canonicalFilePath());
    impl_->lock = std::make_unique<QLockFile>(qpath(impl_->root / "workspace.lock"));
    impl_->lock->setStaleLockTime(0);
    if (!impl_->lock->tryLock()) throw Error(ErrorCode::workspace_busy);
    for (const auto* directory : {"database", "originals", "staging", "batches", "generated", "exports", "templates", "logs"}) {
        const auto path = qpath(impl_->root / directory);
        if (QFileInfo(path).isSymLink() || !QDir().mkpath(path)) throw Error(ErrorCode::file_io);
    }
    SqliteRepository repository(impl_->root / "database" / "muzakere.sqlite3");
}
LocalWorkspace::~LocalWorkspace() = default;

void LocalWorkspace::reset_database_for_testing() {
    std::lock_guard guard(impl_->writer);
    SqliteRepository repository(impl_->root / "database" / "muzakere.sqlite3");
    repository.reset_for_testing();
}

ImportResult LocalWorkspace::import_folder(const std::filesystem::path& folder) {
    std::lock_guard guard(impl_->writer);
    LocalFiles files(impl_->root);
    SqliteRepository repository(impl_->root / "database" / "muzakere.sqlite3");
    SystemIdentity identity;
    return ImportBatch(files, repository, identity).execute(folder);
}
std::vector<ProcessingBatch> LocalWorkspace::batches() {
    SqliteRepository repository(impl_->root / "database" / "muzakere.sqlite3");
    return repository.batches();
}
std::vector<IncomingDocument> LocalWorkspace::documents(const std::string& batch_id) {
    SqliteRepository repository(impl_->root / "database" / "muzakere.sqlite3");
    return repository.documents(batch_id);
}
std::vector<ReviewIssue> LocalWorkspace::issues(const std::string& batch_id) {
    SqliteRepository repository(impl_->root / "database" / "muzakere.sqlite3");
    return repository.issues(batch_id);
}

ImportResult LocalWorkspace::import_archive(const std::filesystem::path& archive) {
    std::lock_guard guard(impl_->writer);
    if (QFileInfo(qpath(archive)).size() > 256LL * 1024 * 1024) throw Error(ErrorCode::invalid_input);
    LocalFiles files(impl_->root);
    const auto stored = files.preserve(archive);
    QFile input(qpath(impl_->root) + '/' + QString::fromStdString(stored.managed_path));
    if (!input.open(QIODevice::ReadOnly)) throw Error(ErrorCode::file_io);
    const auto bytes = input.readAll();
    if (static_cast<std::uint64_t>(bytes.size()) != stored.size ||
        QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex().toStdString() != stored.sha256)
        throw Error(ErrorCode::integrity);
    QTemporaryDir temporary;
    if (!temporary.isValid()) throw Error(ErrorCode::file_io);
    extract_zip(bytes, native_path(temporary.path()));
    SqliteRepository repository(impl_->root / "database" / "muzakere.sqlite3");
    SystemIdentity identity;
    return ImportBatch(files, repository, identity).execute(native_path(temporary.path()),
        QFileInfo(qpath(archive)).fileName().toStdString(), stored);
}

std::vector<AccountingRow> LocalWorkspace::prepare_accounting(const std::string& batch_id) {
    std::lock_guard guard(impl_->writer);
    SqliteRepository repository(impl_->root / "database" / "muzakere.sqlite3");
    auto existing = load_accounting(repository,batch_id);
    if (!existing.empty()) return existing; // Never overwrite reviewed data with a rerun.
    const auto documents = repository.documents(batch_id);
    if (documents.empty()) throw Error(ErrorCode::invalid_input);
    std::vector<ParsedDocument> parsed;
    for (const auto& document : documents) {
        if (document.file.mime_type != "application/pdf" &&
            !QString::fromStdString(document.original_filename).endsWith(".pdf",Qt::CaseInsensitive)) continue;
        const auto source = qpath(impl_->root) + '/' + QString::fromStdString(document.file.managed_path);
        QFile file(source);
        PdfText text;
        if (document.file.size > 64ULL * 1024 * 1024) text.error = "PDF boyut sınırı aşıldı";
        else if (QFileInfo(source).isSymLink() || !file.open(QIODevice::ReadOnly)) text.error = "Kaynak PDF okunamadı";
        else {
            const auto bytes = file.readAll();
            if (QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex().toStdString() != document.file.sha256)
                text.error = "Kaynak PDF bütünlük kontrolü başarısız";
            else text = extract_pdf(bytes);
        }
        parsed.push_back(parse_document(document,text));
    }
    if (parsed.empty()) throw Error(ErrorCode::invalid_input);
    auto rows = match_accounting(std::move(parsed));
    const auto temporary_service_date = QDate::currentDate().toString("dd.MM.yyyy").toStdString();
    for (auto& row : rows) apply_temporary_service_date(row,temporary_service_date);
    repository.save_accounting(batch_id,rows,false);
    return rows;
}

std::vector<AccountingRow> LocalWorkspace::accounting_rows(const std::string& batch_id) {
    std::lock_guard guard(impl_->writer);
    SqliteRepository repository(impl_->root / "database" / "muzakere.sqlite3");
    return load_accounting(repository,batch_id);
}

void LocalWorkspace::review_accounting(const std::string& batch_id, const std::vector<AccountingRow>& edits) {
    std::lock_guard guard(impl_->writer);
    SqliteRepository repository(impl_->root / "database" / "muzakere.sqlite3");
    const auto current = repository.accounting_rows(batch_id);
    std::vector<AccountingRow> reviewed;
    std::set<std::string> ids;
    for (const auto& edit : edits) {
        if(!ids.insert(edit.id).second) throw Error(ErrorCode::invalid_input);
        const auto found = std::find_if(current.begin(),current.end(),[&](const auto& row){return row.id == edit.id;});
        if (found == current.end()) throw Error(ErrorCode::invalid_input);
        auto row = *found; // Keep source identity, confidence and extraction evidence immutable.
        row.cells = edit.cells;
        row.approved = edit.approved;
        if (!row.cells[service_date].empty()) row.warnings.erase(std::remove(row.warnings.begin(),row.warnings.end(),
            "Tebliğ tarihi belgede yok; kullanıcı girişi gerekli"),row.warnings.end());
        else if(std::find(row.warnings.begin(),row.warnings.end(),"Tebliğ tarihi belgede yok; kullanıcı girişi gerekli")==row.warnings.end())
            row.warnings.push_back("Tebliğ tarihi belgede yok; kullanıcı girişi gerekli");
        const auto date = QString::fromStdString(row.cells[service_date]);
        if (!date.isEmpty() && (!QDate::fromString(date,"dd.MM.yyyy").isValid() || date.size() != 10))
            throw Error(ErrorCode::invalid_input);
        if (row.approved && !can_approve(row)) throw Error(ErrorCode::invalid_input);
        for (std::size_t column=0; column<column_count; ++column) {
            if (row.cells[column].size() > 16000) throw Error(ErrorCode::invalid_input);
            if (row.cells[column] != found->cells[column]) row.evidence.push_back({column,row.document_id,
                row.cells[column],"manual-review",1.0});
        }
        reviewed.push_back(std::move(row));
    }
    repository.save_accounting(batch_id,reviewed,true);
}

void LocalWorkspace::export_accounting(const std::string& batch_id, const std::filesystem::path& output, bool draft) {
    export_accounting_impl(batch_id,output,draft,false);
}
void LocalWorkspace::export_demo_accounting(const std::string& batch_id,const std::filesystem::path& output) {
    export_accounting_impl(batch_id,output,true,true);
}
void LocalWorkspace::export_hamdata(const std::string& batch_id,const std::filesystem::path& output,bool with_accounting) {
    export_accounting_impl(batch_id,output,true,false,with_accounting?2:1);
}
void LocalWorkspace::export_accounting_impl(const std::string& batch_id,const std::filesystem::path& output,bool draft,bool demo,int hamdata_mode) {
    std::lock_guard guard(impl_->writer);
    SqliteRepository repository(impl_->root / "database" / "muzakere.sqlite3");
    auto rows = load_accounting(repository,batch_id);
    if (!draft) rows.erase(std::remove_if(rows.begin(),rows.end(),[](const auto& row){return !row.approved;}),rows.end());
    if (rows.empty()) throw Error(ErrorCode::invalid_input);
    std::set<std::string> source_ids;
    for(const auto& row:rows) {source_ids.insert(row.document_id);if(!row.envelope_id.empty())source_ids.insert(row.envelope_id);}
    for(const auto& doc:repository.documents(batch_id)) {
        if(!source_ids.contains(doc.id))continue;
        const auto source=qpath(impl_->root)+'/'+QString::fromStdString(doc.file.managed_path);
        QFile file(source);QCryptographicHash hash(QCryptographicHash::Sha256);
        if(QFileInfo(source).isSymLink() || !file.open(QIODevice::ReadOnly) || !hash.addData(&file) ||
           hash.result().toHex().toStdString()!=doc.file.sha256) throw Error(ErrorCode::integrity);
        source_ids.erase(doc.id);
    }
    if(!source_ids.empty())throw Error(ErrorCode::integrity);
    const QFileInfo target(qpath(output));
    if (target.exists() || !target.fileName().endsWith(".xlsx",Qt::CaseInsensitive)) throw Error(ErrorCode::invalid_input);
    const auto parent = target.absolutePath();
    if (!QDir().mkpath(parent)) throw Error(ErrorCode::file_io);
    const auto canonical = QFileInfo(parent).canonicalFilePath();
    const auto protected_root = qpath(impl_->root);
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    for (const auto* protected_dir : {"originals","database","staging"}) {
        const auto forbidden = protected_root + '/' + protected_dir;
        if (canonical.compare(forbidden,sensitivity)==0 || canonical.startsWith(forbidden+'/',sensitivity))
            throw Error(ErrorCode::invalid_input);
    }
    QTemporaryFile temporary(parent + "/.muz-export-XXXXXX");
    if (!temporary.open()) throw Error(ErrorCode::file_io);
    const auto temporary_path = temporary.fileName(); temporary.close();
    if(hamdata_mode)write_hamdata_xlsx(rows,native_path(temporary_path),hamdata_mode==2);
    else write_accounting_xlsx(rows,native_path(temporary_path),draft,demo);
    QFile content(temporary_path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!content.open(QIODevice::ReadOnly) || !hash.addData(&content)) throw Error(ErrorCode::file_io);
    content.close();
    // Record the exact output hash before publication. Never overwrite an existing output.
    const auto export_id=repository.record_export(batch_id,output,hash.result().toHex().toStdString(),rows.size(),draft);
    if (!temporary.rename(target.absoluteFilePath())) throw Error(ErrorCode::file_io);
    temporary.setAutoRemove(false);
    repository.complete_export(export_id,batch_id,draft);
}
AccountingReturn LocalWorkspace::import_accounting_return(const std::filesystem::path& input) {
    std::lock_guard guard(impl_->writer);
    if(!QFileInfo(qpath(input)).fileName().endsWith(".xlsx",Qt::CaseInsensitive) || QFileInfo(qpath(input)).size()>16*1024*1024)
        throw Error(ErrorCode::invalid_input);
    LocalFiles files(impl_->root); const auto stored=files.preserve(input);
    QFile file(qpath(impl_->root)+'/'+QString::fromStdString(stored.managed_path));
    if(!file.open(QIODevice::ReadOnly))throw Error(ErrorCode::file_io);
    const auto bytes=file.readAll();
    if(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex().toStdString()!=stored.sha256)throw Error(ErrorCode::integrity);
    auto result=read_accounting_return(bytes);
    result.source_name=QFileInfo(qpath(input)).fileName().toStdString();result.sha256=stored.sha256;result.managed_path=stored.managed_path;
    SqliteRepository repository(impl_->root/"database"/"muzakere.sqlite3");repository.save_return(result);return result;
}
std::vector<AccountingReturn> LocalWorkspace::accounting_returns() {
    SqliteRepository repository(impl_->root/"database"/"muzakere.sqlite3");return repository.accounting_returns();
}
std::vector<ResponseRow> LocalWorkspace::response_rows(const std::string& return_id) {
    SqliteRepository repository(impl_->root/"database"/"muzakere.sqlite3");return repository.response_rows(return_id);
}
ResponseProfile LocalWorkspace::response_profile() {
    SqliteRepository repository(impl_->root/"database"/"muzakere.sqlite3");auto profile=repository.response_profile();
    if(!profile.lawyer.empty())return profile;
    QFile defaults(QCoreApplication::applicationDirPath()+"/response-profile.json");
    if(defaults.open(QIODevice::ReadOnly) && defaults.size()<16000) {
        const auto object=QJsonDocument::fromJson(defaults.readAll()).object();
        profile={object["lawyer"].toString().toStdString(),object["address"].toString().toStdString()};
    }
    return profile;
}
void LocalWorkspace::save_response_profile(const ResponseProfile& profile) {
    if(profile.lawyer.size()>1000 || profile.address.size()>4000)throw Error(ErrorCode::invalid_input);
    std::lock_guard guard(impl_->writer);SqliteRepository repository(impl_->root/"database"/"muzakere.sqlite3");repository.save_response_profile(profile);
}
std::string LocalWorkspace::preview_response(const ResponseRow& row,const ResponseProfile& profile) {
    if(!row.errors.empty())throw Error(ErrorCode::invalid_input);
    return response_html(row,profile).toStdString();
}
std::filesystem::path LocalWorkspace::generate_responses(const std::string& return_id,const std::vector<std::string>& row_ids,
        const std::filesystem::path& output,const ResponseProfile& profile) {
    std::lock_guard guard(impl_->writer);
    if(row_ids.empty() || QString::fromStdString(profile.lawyer).trimmed().isEmpty() ||
       QString::fromStdString(profile.address).trimmed().isEmpty() || profile.lawyer.size()>1000 || profile.address.size()>4000)
        throw Error(ErrorCode::invalid_input);
    SqliteRepository repository(impl_->root/"database"/"muzakere.sqlite3");
    const auto returns=repository.accounting_returns();
    const auto data=std::find_if(returns.begin(),returns.end(),[&](const auto& item){return item.id==return_id;});
    if(data==returns.end())throw Error(ErrorCode::invalid_input);
    QFile original(qpath(impl_->root)+'/'+QString::fromStdString(data->managed_path));
    if(!original.open(QIODevice::ReadOnly) || original.size()>16*1024*1024 ||
        QCryptographicHash::hash(original.readAll(),QCryptographicHash::Sha256).toHex().toStdString()!=data->sha256)
        throw Error(ErrorCode::integrity);
    const auto rows=repository.response_rows(return_id);std::set<std::string> selected;
    for(const auto& id:row_ids) {
        const auto found=std::find_if(rows.begin(),rows.end(),[&](const auto& row){return row.id==id;});
        if(!selected.insert(id).second || found==rows.end() || !found->errors.empty())throw Error(ErrorCode::invalid_input);
    }
    const auto target_root=output.empty()?impl_->root/"generated"/"responses":output;
    if(!QDir().mkpath(qpath(target_root)))throw Error(ErrorCode::file_io);
    const auto parent=QFileInfo(qpath(target_root)).canonicalFilePath();
    for(const auto* name:{"originals","database","staging"}) {
        const auto forbidden=qpath(impl_->root)+'/'+name;
        if(parent.compare(forbidden,Qt::CaseInsensitive)==0 || parent.startsWith(forbidden+'/',Qt::CaseInsensitive))throw Error(ErrorCode::invalid_input);
    }
    const auto run=QUuid::createUuid().toString(QUuid::WithoutBraces);
    const auto final=parent+"/Cevaplar-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss")+'-'+run.left(8);
    if(QFileInfo::exists(final))throw Error(ErrorCode::file_io);
    QTemporaryDir temporary(parent+"/.muz-pdf-XXXXXX");if(!temporary.isValid())throw Error(ErrorCode::file_io);
    std::vector<ResponseExport> exports;QJsonArray manifest;
    for(const auto& row:rows) {
        if(!selected.contains(row.id))continue;
        const auto name=QString::fromStdString(normalize_filename(row.source.cells[case_number]+"_"+row.source.cells[debtor])).left(80)+
            '_'+QString::fromStdString(row.id).left(8)+(row.available_cents?"_VAR.pdf":"_YOK.pdf");
        const auto html=response_html(row,profile);
        write_response_pdf(html,native_path(temporary.path()+'/'+name));
        QFile generated(temporary.path()+'/'+name);if(!generated.open(QIODevice::ReadOnly))throw Error(ErrorCode::file_io);
        const auto hash=QCryptographicHash::hash(generated.readAll(),QCryptographicHash::Sha256).toHex().toStdString();
        exports.push_back({QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(),row.id,(final+'/'+name).toStdString(),hash,
            QCryptographicHash::hash(html.toUtf8(),QCryptographicHash::Sha256).toHex().toStdString()});
        manifest.append(QJsonObject{{"record_id",QString::fromStdString(row.source.id)},{"pdf",name},
            {"decision",row.available_cents?"VAR":"YOK"},{"test",row.demo},{"sha256",QString::fromStdString(hash)}});
    }
    QFile summary(temporary.path()+"/manifest.json");
    const auto bytes=QJsonDocument(manifest).toJson();
    if(!summary.open(QIODevice::WriteOnly|QIODevice::NewOnly) || summary.write(bytes)!=bytes.size() || !summary.flush())throw Error(ErrorCode::file_io);
    summary.close();repository.save_response_profile(profile);repository.prepare_response_exports(return_id,exports);
    if(!QDir().rename(temporary.path(),final))throw Error(ErrorCode::file_io);
    temporary.setAutoRemove(false);repository.complete_response_exports(return_id,exports);
    return native_path(final);
}
} // namespace muz
