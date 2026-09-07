#include "local_files.hpp"
#include "qt_paths.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QTemporaryFile>
#include <algorithm>

namespace muz {
namespace {
bool within(const QString& path, const QString& directory) {
#ifdef Q_OS_WIN
    constexpr auto sensitivity = Qt::CaseInsensitive;
#else
    constexpr auto sensitivity = Qt::CaseSensitive;
#endif
    return path.compare(directory, sensitivity) == 0 ||
        path.startsWith(directory + '/', sensitivity);
}

QByteArray hash_file(const QString& path) {
    QFile file(path);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!file.open(QIODevice::ReadOnly) || !hash.addData(&file)) throw Error(ErrorCode::file_io);
    return hash.result().toHex();
}
} // namespace

std::vector<std::filesystem::path> LocalFiles::scan(const std::filesystem::path& folder) {
    const QFileInfo input(qpath(folder));
    const auto canonical = input.canonicalFilePath();
    const auto managed = QFileInfo(qpath(root_)).canonicalFilePath();
    if (!input.isDir() || input.isSymLink() || canonical.isEmpty() ||
        within(canonical, managed) || within(managed, canonical)) throw Error(ErrorCode::invalid_input);
    std::vector<std::filesystem::path> paths;
    // Do not follow directory links. A link itself becomes a visible review issue.
    QDirIterator it(canonical, QDir::Files | QDir::Dirs | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const auto info = it.fileInfo();
        if (!info.isDir() || info.isSymLink()) paths.push_back(native_path(info.absoluteFilePath()));
        else if (!info.isReadable()) throw Error(ErrorCode::file_io);
    }
    std::sort(paths.begin(), paths.end());
    return paths;
}

StoredFile LocalFiles::preserve(const std::filesystem::path& source) {
    const QFileInfo info(qpath(source));
    if (!info.isFile() || info.isSymLink()) throw Error(ErrorCode::invalid_input);
    QFile input(info.absoluteFilePath());
    if (!input.open(QIODevice::ReadOnly)) throw Error(ErrorCode::file_io);
    const auto initial_size = input.size();
    const auto initial_modified = info.lastModified();
    QTemporaryFile staged(qpath(root_ / "staging") + "/import-XXXXXX");
    if (!staged.open()) throw Error(ErrorCode::file_io);
    QCryptographicHash hash(QCryptographicHash::Sha256);
    std::uint64_t bytes = 0;
    while (!input.atEnd()) {
        const auto chunk = input.read(1024 * 1024);
        if (chunk.isEmpty() && input.error() != QFileDevice::NoError) throw Error(ErrorCode::file_io);
        if (staged.write(chunk) != chunk.size()) throw Error(ErrorCode::file_io);
        hash.addData(chunk);
        bytes += static_cast<std::uint64_t>(chunk.size());
    }
    if (!staged.flush()) throw Error(ErrorCode::file_io);
    const auto digest = hash.result().toHex();
    staged.close();
    input.close();
    const QFileInfo after(info.absoluteFilePath());
    // Re-read source and staged bytes before publishing; an unstable source is review-only.
    if (initial_size != static_cast<qint64>(bytes) || after.size() != initial_size ||
        after.lastModified() != initial_modified || hash_file(info.absoluteFilePath()) != digest ||
        hash_file(staged.fileName()) != digest) throw Error(ErrorCode::integrity);
    const auto relative = "originals/" + digest.left(2) + "/" + digest;
    const auto destination = qpath(root_) + '/' + QString::fromLatin1(relative);
    const auto parent = QFileInfo(destination).absolutePath();
    if (QFileInfo(parent).isSymLink() || !QDir().mkpath(parent) ||
        !within(QFileInfo(parent).canonicalFilePath(), QFileInfo(qpath(root_ / "originals")).canonicalFilePath()))
        throw Error(ErrorCode::integrity);
    if (QFileInfo::exists(destination)) {
        if (QFileInfo(destination).isSymLink() || hash_file(destination) != digest) throw Error(ErrorCode::integrity);
    } else {
        // QFile::rename refuses to overwrite. Staging and originals share a filesystem.
        if (!staged.rename(destination)) throw Error(ErrorCode::file_io);
        staged.setAutoRemove(false);
    }
    QMimeDatabase mime;
    return {digest.toStdString(), relative.toStdString(), bytes,
        mime.mimeTypeForFile(destination, QMimeDatabase::MatchContent).name().toStdString()};
}
} // namespace muz
