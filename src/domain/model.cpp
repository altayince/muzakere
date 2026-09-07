#include "muz/domain/model.hpp"

#include <algorithm>

namespace muz {
namespace {
const char* message(ErrorCode code) {
    switch (code) {
    case ErrorCode::invalid_input: return "invalid_input";
    case ErrorCode::file_io: return "file_io";
    case ErrorCode::integrity: return "integrity_check_failed";
    case ErrorCode::storage: return "storage_failed";
    case ErrorCode::schema_version: return "unsupported_schema_version";
    case ErrorCode::workspace_busy: return "workspace_busy";
    }
    return "unknown_error";
}
} // namespace

Error::Error(ErrorCode code) : std::runtime_error(message(code)), code_(code) {}

std::string normalize_filename(std::string name) {
    for (char& value : name) {
        const auto c = static_cast<unsigned char>(value);
        if (c < 32 || c == 127 || std::string("<>:\"/\\|?*").find(value) != std::string::npos) {
            value = '_';
        }
    }
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) name.pop_back();
    if (name.empty()) return "unnamed";
    return name;
}

bool valid_sha256(const std::string& hash) {
    return hash.size() == 64 && std::all_of(hash.begin(), hash.end(), [](char c) {
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    });
}
} // namespace muz
