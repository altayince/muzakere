#include "muz/infrastructure/local_workspace.hpp"
#include "muz/application/import_batch.hpp"
#include "local_files.hpp"
#include "qt_paths.hpp"
#include "sqlite_repository.hpp"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QLockFile>
#include <QUuid>
#include <mutex>

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
} // namespace muz
