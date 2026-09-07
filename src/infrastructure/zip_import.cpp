#include "accounting_adapters.hpp"
#include "qt_paths.hpp"
#include <miniz.h>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QRegularExpression>

namespace muz {
void extract_zip(const QByteArray& bytes, const std::filesystem::path& destination) {
    mz_zip_archive zip{};
    if (!mz_zip_reader_init_mem(&zip, bytes.constData(), static_cast<size_t>(bytes.size()), 0))
        throw Error(ErrorCode::invalid_input);
    struct Guard { mz_zip_archive* zip; ~Guard() { mz_zip_reader_end(zip); } } guard{&zip};
    const auto count = mz_zip_reader_get_num_files(&zip);
    if (count == 0 || count > 10000) throw Error(ErrorCode::invalid_input);
    std::uint64_t total = 0;
    QSet<QString> names;
    // Validate the complete central directory before writing any member.
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) throw Error(ErrorCode::integrity);
        const auto length = mz_zip_reader_get_filename(&zip, i, nullptr, 0);
        if (length > sizeof(stat.m_filename) || length == 0) throw Error(ErrorCode::invalid_input);
        QByteArray raw(static_cast<qsizetype>(length), '\0');
        mz_zip_reader_get_filename(&zip, i, raw.data(), length);
        raw.chop(1);
        if (!raw.isValidUtf8() || raw.contains('\0')) throw Error(ErrorCode::invalid_input);
        auto name = QString::fromUtf8(raw);
        name.replace('\\', '/');
        if (name.startsWith('/') || name.contains(':') || stat.m_is_encrypted ||
            ((stat.m_external_attr >> 16) & 0170000) == 0120000) throw Error(ErrorCode::invalid_input);
        auto parts = name.split('/');
        if (stat.m_is_directory && !parts.empty() && parts.back().isEmpty()) parts.removeLast();
        for (const auto& part : parts) {
            if (part.isEmpty() || part == "." || part == ".." || part.endsWith('.') || part.endsWith(' ') ||
                part.contains(QRegularExpression(R"([<>"|?*\x00-\x1f])")) ||
                QRegularExpression(R"(^(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])(?:\.|$))", QRegularExpression::CaseInsensitiveOption).match(part).hasMatch())
                throw Error(ErrorCode::invalid_input);
        }
        const auto key = parts.join('/').normalized(QString::NormalizationForm_C).toCaseFolded();
        if (names.contains(key)) throw Error(ErrorCode::invalid_input);
        names.insert(key);
        if (stat.m_uncomp_size > 64ULL * 1024 * 1024) throw Error(ErrorCode::invalid_input);
        total += stat.m_uncomp_size;
        if (total > 512ULL * 1024 * 1024) throw Error(ErrorCode::invalid_input);
    }
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat stat{};
        if (!mz_zip_reader_file_stat(&zip, i, &stat)) throw Error(ErrorCode::integrity);
        auto name = QString::fromUtf8(stat.m_filename).replace('\\', '/');
        const auto target = qpath(destination) + '/' + name;
        if (stat.m_is_directory) {
            if (!QDir().mkpath(target)) throw Error(ErrorCode::file_io);
            continue;
        }
        if (!QDir().mkpath(QFileInfo(target).absolutePath())) throw Error(ErrorCode::file_io);
        QByteArray data(static_cast<qsizetype>(stat.m_uncomp_size), Qt::Uninitialized);
        if (!mz_zip_reader_extract_to_mem(&zip, i, data.data(), static_cast<size_t>(data.size()), 0))
            throw Error(ErrorCode::integrity); // Includes CRC verification.
        QFile file(target);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || file.write(data) != data.size())
            throw Error(ErrorCode::file_io);
    }
}
} // namespace muz
