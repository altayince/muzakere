#include "muz/infrastructure/local_workspace.hpp"
#include "muz/application/import_batch.hpp"
#include "local_files.hpp"
#include "qt_paths.hpp"
#include "sqlite_repository.hpp"
#include "accounting_adapters.hpp"

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

namespace muz {
namespace {
class SystemIdentity final : public IdentityClock {
public:
    std::string new_id() override { return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString(); }
    std::string now_utc() override { return QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString(); }
};
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
    auto existing = repository.accounting_rows(batch_id);
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
    repository.save_accounting(batch_id,rows,false);
    return rows;
}

std::vector<AccountingRow> LocalWorkspace::accounting_rows(const std::string& batch_id) {
    SqliteRepository repository(impl_->root / "database" / "muzakere.sqlite3");
    return repository.accounting_rows(batch_id);
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
    std::lock_guard guard(impl_->writer);
    SqliteRepository repository(impl_->root / "database" / "muzakere.sqlite3");
    auto rows = repository.accounting_rows(batch_id);
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
    write_accounting_xlsx(rows,native_path(temporary_path),draft);
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
} // namespace muz
