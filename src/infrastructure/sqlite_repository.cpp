#include "sqlite_repository.hpp"
#include "qt_paths.hpp"
#include "schema.hpp"
#include "accounting_adapters.hpp"
#include "response_adapters.hpp"

#include <QSqlQuery>
#include <QUuid>
#include <QVariant>
#include <QDateTime>
#include <algorithm>
#include <tuple>

namespace muz {
namespace {
QSqlQuery query(QSqlDatabase& db, const QString& sql, const QVariantList& bindings = {}) {
    QSqlQuery result(db);
    if (!result.prepare(sql)) throw Error(ErrorCode::storage);
    for (const auto& binding : bindings) result.addBindValue(binding);
    if (!result.exec()) throw Error(ErrorCode::storage);
    return result;
}

class Transaction {
public:
    explicit Transaction(QSqlDatabase& db) : db_(db) {
        query(db_, "BEGIN IMMEDIATE");
    }
    ~Transaction() { if (!committed_) db_.rollback(); }
    void commit() {
        if (!db_.commit()) throw Error(ErrorCode::storage);
        committed_ = true;
    }
private:
    QSqlDatabase& db_;
    bool committed_ = false;
};

QString s(const std::string& value) { return QString::fromStdString(value); }
void audit(QSqlDatabase& db, const std::string& time, const char* event,
           const std::string& entity, const std::string& batch) {
    query(db, "INSERT INTO audit_events(occurred_at,event_type,entity_id,batch_id) VALUES(?,?,?,?)",
        {s(time), QString::fromLatin1(event), s(entity), s(batch)});
}
} // namespace

SqliteRepository::SqliteRepository(const std::filesystem::path& database) {
    const auto connection = QUuid::createUuid().toString(QUuid::WithoutBraces);
    db_ = QSqlDatabase::addDatabase("QSQLITE", connection);
    try {
        db_.setDatabaseName(qpath(database));
        db_.setConnectOptions("QSQLITE_BUSY_TIMEOUT=5000");
        if (!db_.open()) throw Error(ErrorCode::storage);
        query(db_, "PRAGMA foreign_keys=ON");
        query(db_, "PRAGMA journal_mode=WAL");
        query(db_, "PRAGMA synchronous=FULL");
        migrate();
    } catch (...) {
        db_.close();
        db_ = QSqlDatabase();
        QSqlDatabase::removeDatabase(connection);
        throw;
    }
}

SqliteRepository::~SqliteRepository() {
    const auto connection = db_.connectionName();
    db_.close();
    db_ = QSqlDatabase();
    QSqlDatabase::removeDatabase(connection);
}

void SqliteRepository::migrate() {
    Transaction tx(db_);
    auto version = query(db_, "PRAGMA user_version");
    if (!version.next()) throw Error(ErrorCode::storage);
    const auto current = version.value(0).toInt();
    version.finish();
    if (current > 4 || current < 0) throw Error(ErrorCode::schema_version);
    if (current == 0) {
        const auto statements = QString::fromUtf8(schema_v1).split("-- statement", Qt::SkipEmptyParts);
        for (const auto& statement : statements) query(db_, statement);
    } else {
        auto check = query(db_, "SELECT version FROM schema_migrations ORDER BY version");
        for (int expected = 1; expected <= current; ++expected)
            if (!check.next() || check.value(0).toInt() != expected) throw Error(ErrorCode::schema_version);
        if (check.next()) throw Error(ErrorCode::schema_version);
        check.finish();
    }
    if (current < 2) {
        for (const auto& statement : QString::fromUtf8(schema_v2).split("-- statement", Qt::SkipEmptyParts))
            query(db_, statement);
    }
    if (current < 3) {
        for (const auto& statement : QString::fromUtf8(schema_v3).split("-- statement", Qt::SkipEmptyParts))
            query(db_, statement);
    }
    if (current < 4) {
        for (const auto& statement : QString::fromUtf8(schema_v4).split("-- statement", Qt::SkipEmptyParts))
            query(db_, statement);
    }
    tx.commit();
}

void SqliteRepository::reset_for_testing() {
    // Reset only this workspace's database, atomically. DROP also removes the
    // append-only audit triggers; the schema scripts recreate them before commit.
    Transaction tx(db_);
    for (const auto* table : {"response_exports", "response_rows", "response_events", "response_profile", "accounting_returns",
            "accounting_exports", "accounting_rows", "batch_archives",
            "audit_events", "review_issues", "incoming_documents", "case_records",
            "stored_files", "processing_batches", "schema_migrations"})
        query(db_, "DROP TABLE " + QString::fromLatin1(table));
    for (const auto* schema : {schema_v1, schema_v2, schema_v3, schema_v4})
        for (const auto& statement : QString::fromUtf8(schema).split("-- statement", Qt::SkipEmptyParts))
            query(db_, statement);
    tx.commit();
}

void SqliteRepository::save(ImportResult& result) {
    Transaction tx(db_);
    auto& batch = result.batch;
    query(db_, "INSERT INTO processing_batches VALUES(?,?,?,?,?,?)",
        {s(batch.id), s(batch.created_at), s(batch.status),
         QVariant::fromValue(static_cast<qlonglong>(batch.imported_count)), 0,
         QVariant::fromValue(static_cast<qlonglong>(batch.issue_count))});
    audit(db_, batch.created_at, "batch_imported", batch.id, batch.id);
    if (!result.archive_name.empty()) {
        query(db_, "INSERT INTO batch_archives VALUES(?,?,?,?,?)", {s(batch.id), s(result.archive_name),
            s(result.archive_file.sha256), s(result.archive_file.managed_path),
            QVariant::fromValue(static_cast<qlonglong>(result.archive_file.size))});
        audit(db_, batch.created_at, "zip_imported", batch.id, batch.id);
    }
    batch.duplicate_count = 0;
    for (auto& doc : result.documents) {
        if (!valid_sha256(doc.file.sha256)) throw Error(ErrorCode::integrity);
        auto existing = query(db_, "SELECT size_bytes,managed_path FROM stored_files WHERE sha256=?", {s(doc.file.sha256)});
        doc.duplicate = existing.next();
        if (doc.duplicate && (existing.value(0).toULongLong() != doc.file.size ||
            existing.value(1).toString() != s(doc.file.managed_path))) throw Error(ErrorCode::integrity);
        existing.finish();
        if (doc.duplicate) ++batch.duplicate_count;
        else query(db_, "INSERT INTO stored_files VALUES(?,?,?,?)",
            {s(doc.file.sha256), s(doc.file.managed_path), QVariant::fromValue(static_cast<qlonglong>(doc.file.size)), s(doc.file.mime_type)});
        query(db_, "INSERT INTO incoming_documents(id,batch_id,file_sha256,original_filename,normalized_filename,source_path,imported_at,is_duplicate) VALUES(?,?,?,?,?,?,?,?)",
            {s(doc.id), s(batch.id), s(doc.file.sha256), s(doc.original_filename), s(doc.normalized_filename),
             s(doc.source_path), s(doc.imported_at), doc.duplicate ? 1 : 0});
        audit(db_, doc.imported_at, doc.duplicate ? "duplicate_imported" : "document_imported", doc.id, batch.id);
    }
    for (const auto& issue : result.issues) {
        query(db_, "INSERT INTO review_issues(id,batch_id,source_path,code) VALUES(?,?,?,?)",
            {s(issue.id), s(batch.id), s(issue.source_path), s(issue.code)});
        audit(db_, batch.created_at, "import_needs_review", issue.id, batch.id);
    }
    query(db_, "UPDATE processing_batches SET duplicate_count=? WHERE id=?",
        {QVariant::fromValue(static_cast<qlonglong>(batch.duplicate_count)), s(batch.id)});
    tx.commit();
}

std::vector<ProcessingBatch> SqliteRepository::batches() {
    auto rows = query(db_, "SELECT id,created_at,status,imported_count,duplicate_count,issue_count FROM processing_batches ORDER BY created_at DESC,id");
    std::vector<ProcessingBatch> result;
    while (rows.next()) result.push_back({rows.value(0).toString().toStdString(), rows.value(1).toString().toStdString(),
        rows.value(2).toString().toStdString(), rows.value(3).toULongLong(), rows.value(4).toULongLong(), rows.value(5).toULongLong()});
    return result;
}

std::vector<IncomingDocument> SqliteRepository::documents(const std::string& batch_id) {
    auto rows = query(db_, "SELECT d.id,d.batch_id,d.original_filename,d.normalized_filename,d.source_path,d.imported_at,f.sha256,f.managed_path,f.size_bytes,f.mime_type,d.is_duplicate FROM incoming_documents d JOIN stored_files f ON f.sha256=d.file_sha256 WHERE d.batch_id=? ORDER BY d.source_path,d.id", {s(batch_id)});
    std::vector<IncomingDocument> result;
    while (rows.next()) result.push_back({rows.value(0).toString().toStdString(), rows.value(1).toString().toStdString(),
        rows.value(2).toString().toStdString(), rows.value(3).toString().toStdString(), rows.value(4).toString().toStdString(),
        rows.value(5).toString().toStdString(), {rows.value(6).toString().toStdString(), rows.value(7).toString().toStdString(),
        rows.value(8).toULongLong(), rows.value(9).toString().toStdString()}, rows.value(10).toBool()});
    return result;
}

std::vector<ReviewIssue> SqliteRepository::issues(const std::string& batch_id) {
    auto rows = query(db_, "SELECT id,batch_id,source_path,code FROM review_issues WHERE batch_id=? AND status='open' ORDER BY id", {s(batch_id)});
    std::vector<ReviewIssue> result;
    while (rows.next()) result.push_back({rows.value(0).toString().toStdString(), rows.value(1).toString().toStdString(),
        rows.value(2).toString().toStdString(), rows.value(3).toString().toStdString()});
    return result;
}

std::vector<AccountingRow> SqliteRepository::accounting_rows(const std::string& batch_id) {
    auto rows = query(db_, "SELECT payload FROM accounting_rows WHERE batch_id=? ORDER BY id", {s(batch_id)});
    std::vector<AccountingRow> result;
    while (rows.next()) result.push_back(decode_row(rows.value(0).toString()));
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        return std::tie(a.source_path,a.document_id,a.debtor_index,a.id)<std::tie(b.source_path,b.document_id,b.debtor_index,b.id);
    });
    return result;
}

void SqliteRepository::save_accounting(const std::string& batch_id, const std::vector<AccountingRow>& rows, bool review) {
    Transaction tx(db_);
    const auto now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
    for (const auto& row : rows) {
        if (row.batch_id != batch_id || row.id.empty() || row.document_id.empty()) throw Error(ErrorCode::invalid_input);
        auto source=query(db_,"SELECT 1 FROM incoming_documents WHERE id=? AND batch_id=?",{s(row.document_id),s(batch_id)});
        if(!source.next())throw Error(ErrorCode::invalid_input);
        source.finish();
        const auto statement = review ?
            "UPDATE accounting_rows SET payload=?,approved=? WHERE id=? AND batch_id=? AND document_id=?" :
            "INSERT INTO accounting_rows(payload,approved,id,batch_id,document_id) VALUES(?,?,?,?,?)";
        auto write = query(db_, statement, {encode_row(row), row.approved ? 1 : 0, s(row.id), s(batch_id),s(row.document_id)});
        if (write.numRowsAffected() != 1) throw Error(ErrorCode::storage);
        audit(db_, now, review ? (row.approved ? "accounting_row_approved" : "extracted_fields_edited") :
              "fields_extracted", row.id, batch_id);
        if (!review) audit(db_, now, row.envelope_id.empty() ? "matching_needs_review" : "matching_proposed", row.id, batch_id);
    }
    tx.commit();
}

void SqliteRepository::replace_accounting(const std::string& batch_id, const std::vector<AccountingRow>& rows) {
    Transaction tx(db_);
    query(db_,"DELETE FROM accounting_rows WHERE batch_id=?",{s(batch_id)});
    const auto now=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
    for(const auto& row:rows) {
        if(row.batch_id!=batch_id)throw Error(ErrorCode::invalid_input);
        auto source=query(db_,"SELECT 1 FROM incoming_documents WHERE id=? AND batch_id=?",{s(row.document_id),s(batch_id)});
        if(!source.next())throw Error(ErrorCode::invalid_input);
        source.finish();
        query(db_,"INSERT INTO accounting_rows(id,document_id,batch_id,payload,approved) VALUES(?,?,?,?,?)",
            {s(row.id),s(row.document_id),s(batch_id),encode_row(row),row.approved?1:0});
        audit(db_,now,"debtor_rows_migrated",row.id,batch_id);
    }
    tx.commit();
}

std::string SqliteRepository::record_export(const std::string& batch_id, const std::filesystem::path& path,
                                    const std::string& sha256, std::size_t count, bool draft) {
    Transaction tx(db_);
    const auto id = QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
    const auto now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
    query(db_, "INSERT INTO accounting_exports(id,batch_id,created_at,output_path,sha256,row_count,is_draft) VALUES(?,?,?,?,?,?,?)", {s(id),s(batch_id),s(now),qpath(path),s(sha256),
          QVariant::fromValue(static_cast<qlonglong>(count)),draft ? 1 : 0});
    audit(db_, now, "accounting_export_prepared", id, batch_id);
    tx.commit();
    return id;
}
void SqliteRepository::complete_export(const std::string& id, const std::string& batch_id, bool draft) {
    Transaction tx(db_);
    auto result=query(db_,"UPDATE accounting_exports SET status='published' WHERE id=? AND batch_id=? AND status='prepared'",{s(id),s(batch_id)});
    if(result.numRowsAffected()!=1) throw Error(ErrorCode::storage);
    audit(db_,QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString(),
          draft ? "accounting_draft_exported":"accounting_exported",id,batch_id);
    tx.commit();
}
void SqliteRepository::save_return(const AccountingReturn& data) {
    Transaction tx(db_);
    query(db_,"INSERT INTO accounting_returns VALUES(?,?,?,?,?)",{s(data.id),s(data.source_name),s(data.created_at),s(data.sha256),s(data.managed_path)});
    int index=0;
    for(const auto& row:data.rows) {
        if(row.return_id!=data.id)throw Error(ErrorCode::invalid_input);
        query(db_,"INSERT INTO response_rows VALUES(?,?,?,?)",{s(row.id),s(data.id),index++,encode_response(row)});
    }
    query(db_,"INSERT INTO response_events(return_id,occurred_at,event_type,entity_id) VALUES(?,?,'return_imported',?)",
        {s(data.id),s(data.created_at),s(data.id)});
    tx.commit();
}
std::vector<AccountingReturn> SqliteRepository::accounting_returns() {
    auto data=query(db_,"SELECT id,source_name,created_at,sha256,managed_path FROM accounting_returns ORDER BY created_at DESC,id");
    std::vector<AccountingReturn> result;
    while(data.next())result.push_back({data.value(0).toString().toStdString(),data.value(1).toString().toStdString(),
        data.value(2).toString().toStdString(),data.value(3).toString().toStdString(),data.value(4).toString().toStdString(),{}});
    return result;
}
std::vector<ResponseRow> SqliteRepository::response_rows(const std::string& return_id) {
    auto data=query(db_,"SELECT r.payload,(SELECT output_path FROM response_exports e WHERE e.row_id=r.id AND e.status='published' ORDER BY created_at DESC,id DESC LIMIT 1) FROM response_rows r WHERE return_id=? ORDER BY position",{s(return_id)});
    std::vector<ResponseRow> result;
    while(data.next()){auto row=decode_response(data.value(0).toString());row.output_pdf=data.value(1).toString().toStdString();result.push_back(std::move(row));}
    return result;
}
ResponseProfile SqliteRepository::response_profile() {
    auto data=query(db_,"SELECT lawyer,address FROM response_profile WHERE id=1");
    if(!data.next())return {};
    return {data.value(0).toString().toStdString(),data.value(1).toString().toStdString()};
}
void SqliteRepository::save_response_profile(const ResponseProfile& profile) {
    query(db_,"INSERT INTO response_profile VALUES(1,?,?) ON CONFLICT(id) DO UPDATE SET lawyer=excluded.lawyer,address=excluded.address",
        {s(profile.lawyer),s(profile.address)});
}
void SqliteRepository::prepare_response_exports(const std::string& return_id,const std::vector<ResponseExport>& exports) {
    Transaction tx(db_);const auto now=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    for(const auto& item:exports) {
        auto source=query(db_,"SELECT 1 FROM response_rows WHERE id=? AND return_id=?",{s(item.row_id),s(return_id)});
        if(!source.next())throw Error(ErrorCode::invalid_input);
        source.finish();
        query(db_,"INSERT INTO response_exports VALUES(?,?,?,?,?,?,'prepared')",{s(item.id),s(item.row_id),now,s(item.path),s(item.sha256),s(item.template_sha256)});
        query(db_,"INSERT INTO response_events(return_id,occurred_at,event_type,entity_id) VALUES(?,?,'pdf_prepared',?)",{s(return_id),now,s(item.id)});
    }
    tx.commit();
}
void SqliteRepository::complete_response_exports(const std::string& return_id,const std::vector<ResponseExport>& exports) {
    Transaction tx(db_);const auto now=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
    for(const auto& item:exports) {
        auto updated=query(db_,"UPDATE response_exports SET status='published' WHERE id=? AND status='prepared'",{s(item.id)});
        if(updated.numRowsAffected()!=1)throw Error(ErrorCode::storage);
        query(db_,"INSERT INTO response_events(return_id,occurred_at,event_type,entity_id) VALUES(?,?,'pdf_published',?)",{s(return_id),now,s(item.id)});
    }
    tx.commit();
}
} // namespace muz
