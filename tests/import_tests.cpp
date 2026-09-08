#include "muz/infrastructure/local_workspace.hpp"
#include <catch2/catch_test_macros.hpp>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QUuid>
#include <QVariant>

namespace {
std::filesystem::path path(const QString& value) {
    const auto bytes = value.toUtf8();
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(bytes.constData()),
        static_cast<std::size_t>(bytes.size())));
}
void write(const QString& name, const QByteArray& bytes) {
    REQUIRE(QDir().mkpath(QFileInfo(name).absolutePath()));
    QFile file(name);
    REQUIRE(file.open(QIODevice::WriteOnly));
    REQUIRE(file.write(bytes) == bytes.size());
}
QByteArray read(const QString& name) {
    QFile file(name);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}
struct Fixture {
    QTemporaryDir temp;
    QString root = temp.path() + "/workspace";
    QString input = temp.path() + "/input";
    Fixture() { REQUIRE(temp.isValid()); REQUIRE(QDir().mkpath(input)); }
};
class Db {
public:
    explicit Db(const QString& root) {
        db = QSqlDatabase::addDatabase("QSQLITE", QUuid::createUuid().toString());
        db.setDatabaseName(root + "/database/muzakere.sqlite3");
        REQUIRE(db.open());
    }
    ~Db() {
        const auto name = db.connectionName();
        db.close(); db = QSqlDatabase(); QSqlDatabase::removeDatabase(name);
    }
    qlonglong scalar(const QString& sql) {
        QSqlQuery query(db);
        REQUIRE(query.exec(sql)); REQUIRE(query.next());
        return query.value(0).toLongLong();
    }
    void exec(const QString& sql) { QSqlQuery query(db); REQUIRE(query.exec(sql)); }
    QSqlDatabase db;
};
}

TEST_CASE("Initialize current schema and reopen the persisted workspace", "[integration][database]") {
    Fixture f;
    { muz::LocalWorkspace workspace(path(f.root)); REQUIRE(workspace.batches().empty()); }
    muz::LocalWorkspace reopened(path(f.root));
    Db db(f.root);
    REQUIRE(db.scalar("PRAGMA user_version") == 4);
    REQUIRE(db.scalar("SELECT COUNT(*) FROM schema_migrations") == 4);
    REQUIRE(reopened.batches().empty());
}

TEST_CASE("Import preserves originals, known SHA-256, MIME and metadata across restart", "[integration][import]") {
    Fixture f;
    const auto name = QString::fromUtf8("\xc4\xb0" "cra.txt");
    write(f.input + '/' + name, "abc");
    std::string batch_id;
    {
        muz::LocalWorkspace workspace(path(f.root));
        const auto result = workspace.import_folder(path(f.input));
        batch_id = result.batch.id;
        REQUIRE_FALSE(QUuid(QString::fromStdString(batch_id)).isNull());
        REQUIRE(result.batch.imported_count == 1);
        REQUIRE(result.batch.issue_count == 0);
        REQUIRE(result.documents[0].original_filename == name.toStdString());
        REQUIRE(result.documents[0].file.sha256 == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
        REQUIRE(result.documents[0].file.mime_type == "text/plain");
        REQUIRE(read(f.input + '/' + name) == "abc");
        REQUIRE(read(f.root + '/' + QString::fromStdString(result.documents[0].file.managed_path)) == "abc");
    }
    muz::LocalWorkspace reopened(path(f.root));
    REQUIRE(reopened.batches().size() == 1);
    REQUIRE(reopened.documents(batch_id).size() == 1);
    Db db(f.root);
    REQUIRE(db.scalar("SELECT COUNT(*) FROM audit_events") == 2);
    REQUIRE(db.scalar("SELECT COUNT(*) FROM incoming_documents WHERE case_id IS NOT NULL") == 0);
}

TEST_CASE("Duplicate bytes share storage while each import retains its identity", "[integration][import]") {
    Fixture f;
    write(f.input + "/a.txt", "same");
    write(f.input + "/nested/b.txt", "same");
    muz::LocalWorkspace workspace(path(f.root));
    const auto first = workspace.import_folder(path(f.input));
    REQUIRE(first.batch.imported_count == 2);
    REQUIRE(first.batch.duplicate_count == 1);
    const auto second = workspace.import_folder(path(f.input));
    REQUIRE(second.batch.duplicate_count == 2);
    REQUIRE(first.documents[0].id != second.documents[0].id);
    Db db(f.root);
    REQUIRE(db.scalar("SELECT COUNT(*) FROM stored_files") == 1);
    REQUIRE(db.scalar("SELECT COUNT(*) FROM incoming_documents") == 4);
    REQUIRE(db.scalar("SELECT COUNT(*) FROM processing_batches") == 2);
}

TEST_CASE("Same filename with different bytes does not merge documents", "[integration][import]") {
    Fixture f;
    write(f.input + "/a/content.txt", "one");
    write(f.input + "/b/content.txt", "two");
    muz::LocalWorkspace workspace(path(f.root));
    const auto result = workspace.import_folder(path(f.input));
    REQUIRE(result.batch.imported_count == 2);
    REQUIRE(result.batch.duplicate_count == 0);
    REQUIRE(result.documents[0].file.sha256 != result.documents[1].file.sha256);
}

TEST_CASE("Tampered managed content is never overwritten and becomes Needs Review", "[integration][integrity]") {
    Fixture f;
    write(f.input + "/a.txt", "original");
    muz::LocalWorkspace workspace(path(f.root));
    const auto first = workspace.import_folder(path(f.input));
    const auto managed = f.root + '/' + QString::fromStdString(first.documents[0].file.managed_path);
    write(managed, "tampered");
    const auto second = workspace.import_folder(path(f.input));
    REQUIRE(second.batch.status == "needs_review");
    REQUIRE(second.batch.imported_count == 0);
    REQUIRE(second.batch.issue_count == 1);
    REQUIRE(workspace.issues(second.batch.id)[0].code == "integrity_check_failed");
    REQUIRE(read(managed) == "tampered");
    REQUIRE(read(f.input + "/a.txt") == "original");
}

TEST_CASE("A database failure rolls back batch, documents and audit together", "[integration][database]") {
    Fixture f;
    write(f.input + "/a.txt", "data");
    muz::LocalWorkspace workspace(path(f.root));
    Db db(f.root);
    db.exec("CREATE TRIGGER fail_import BEFORE INSERT ON incoming_documents BEGIN SELECT RAISE(ABORT, 'injected'); END;");
    REQUIRE_THROWS_AS(workspace.import_folder(path(f.input)), muz::Error);
    REQUIRE(db.scalar("SELECT COUNT(*) FROM processing_batches") == 0);
    REQUIRE(db.scalar("SELECT COUNT(*) FROM stored_files") == 0);
    REQUIRE(db.scalar("SELECT COUNT(*) FROM audit_events") == 0);
    db.exec("DROP TRIGGER fail_import");
    REQUIRE(workspace.import_folder(path(f.input)).batch.imported_count == 1);
}

TEST_CASE("Audit rows reject updates and deletes", "[integration][database]") {
    Fixture f;
    write(f.input + "/a.txt", "data");
    muz::LocalWorkspace workspace(path(f.root));
    workspace.import_folder(path(f.input));
    Db db(f.root);
    QSqlQuery query(db.db);
    REQUIRE_FALSE(query.exec("UPDATE audit_events SET event_type='changed'"));
    REQUIRE_FALSE(query.exec("DELETE FROM audit_events"));
    REQUIRE(db.scalar("SELECT COUNT(*) FROM audit_events") == 2);
}

TEST_CASE("Reject empty input, workspace recursion and a second workspace owner", "[integration][validation]") {
    Fixture f;
    muz::LocalWorkspace workspace(path(f.root));
    REQUIRE_THROWS_AS(workspace.import_folder(path(f.input)), muz::Error);
    REQUIRE_THROWS_AS(workspace.import_folder(path(f.root)), muz::Error);
    REQUIRE_THROWS_AS(workspace.import_folder(path(f.temp.path())), muz::Error);
    REQUIRE_THROWS_AS(muz::LocalWorkspace(path(f.root)), muz::Error);
    REQUIRE(workspace.batches().empty());
}

TEST_CASE("Newer schema is rejected without downgrading it", "[integration][database]") {
    Fixture f;
    { muz::LocalWorkspace workspace(path(f.root)); }
    { Db db(f.root); db.exec("PRAGMA user_version=99"); }
    REQUIRE_THROWS_AS(muz::LocalWorkspace(path(f.root)), muz::Error);
    Db db(f.root);
    REQUIRE(db.scalar("PRAGMA user_version") == 99);
}

TEST_CASE("Synthetic fixture imports without extraction or case guessing", "[integration][fixtures]") {
    Fixture f;
    REQUIRE(QFile::copy(QString::fromUtf8(MUZ_FIXTURE_DIR) + "/synthetic-envelope.txt", f.input + "/envelope.txt"));
    muz::LocalWorkspace workspace(path(f.root));
    REQUIRE(workspace.import_folder(path(f.input)).batch.imported_count == 1);
    Db db(f.root);
    REQUIRE(db.scalar("SELECT COUNT(*) FROM case_records") == 0);
}

TEST_CASE("Hundreds of files persist in a single batch with distinct identities", "[integration][batch]") {
    Fixture f;
    for (int index = 0; index < 250; ++index)
        write(f.input + "/" + QString::number(index) + ".txt", "synthetic document " + QByteArray::number(index));
    muz::LocalWorkspace workspace(path(f.root));
    const auto result = workspace.import_folder(path(f.input));
    REQUIRE(result.batch.imported_count == 250);
    REQUIRE(result.batch.duplicate_count == 0);
    REQUIRE(result.batch.issue_count == 0);
    REQUIRE(workspace.documents(result.batch.id).size() == 250);
    Db db(f.root);
    REQUIRE(db.scalar("SELECT COUNT(DISTINCT id) FROM incoming_documents") == 250);
    REQUIRE(db.scalar("SELECT COUNT(*) FROM audit_events") == 251);
}

#ifndef Q_OS_WIN
TEST_CASE("Source links are reported without following them", "[integration][integrity]") {
    Fixture f;
    write(f.input + "/good.txt", "good");
    write(f.temp.path() + "/outside.txt", "outside");
    REQUIRE(QFile::link(f.temp.path() + "/outside.txt", f.input + "/link.txt"));
    muz::LocalWorkspace workspace(path(f.root));
    const auto result = workspace.import_folder(path(f.input));
    REQUIRE(result.batch.imported_count == 1);
    REQUIRE(result.batch.issue_count == 1);
    REQUIRE(result.batch.status == "needs_review");
    REQUIRE(workspace.issues(result.batch.id).size() == 1);
}
#endif
