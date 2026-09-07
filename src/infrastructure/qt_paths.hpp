#pragma once
#include <QString>
#include <filesystem>

namespace muz {
inline QString qpath(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return QString::fromUtf8(reinterpret_cast<const char*>(text.data()), static_cast<qsizetype>(text.size()));
}
inline std::filesystem::path native_path(const QString& value) {
    const auto bytes = value.toUtf8();
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(bytes.constData()),
        static_cast<std::size_t>(bytes.size())));
}
} // namespace muz
