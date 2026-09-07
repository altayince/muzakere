#include "sqlite_repository.hpp"
#include "qt_paths.hpp"
#include "schema.hpp"
#include "accounting_adapters.hpp"

#include <QSqlQuery>
#include <QUuid>
#include <QVariant>
#include <QDateTime>
#include <algorithm>

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
    if (current > 2 || current < 0) throw Error(ErrorCode::schema_version);
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
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) { return a.source_path < b.source_path; });
    return result;
}

void SqliteRepository::save_accounting(const std::string& batch_id, const std::vector<AccountingRow>& rows, bool review) {
    Transaction tx(db_);
    const auto now = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
    for (const auto& row : rows) {
        if (row.batch_id != batch_id || row.id != row.document_id) throw Error(ErrorCode::invalid_input);
        const auto statement = review ?
            "UPDATE accounting_rows SET payload=?,approved=? WHERE id=? AND batch_id=?" :
            "INSERT INTO accounting_rows(payload,approved,id,batch_id) VALUES(?,?,?,?)";
        auto write = query(db_, statement, {encode_row(row), row.approved ? 1 : 0, s(row.id), s(batch_id)});
        if (write.numRowsAffected() != 1) throw Error(ErrorCode::storage);
        audit(db_, now, review ? (row.approved ? "accounting_row_approved" : "extracted_fields_edited") :
              "fields_extracted", row.id, batch_id);
        if (!review) audit(db_, now, row.envelope_id.empty() ? "matching_needs_review" : "matching_proposed", row.id, batch_id);
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
} // namespace muz
