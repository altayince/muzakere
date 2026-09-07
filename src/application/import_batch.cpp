#include "muz/application/import_batch.hpp"

namespace muz {
namespace {
std::string utf8(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}
} // namespace

ImportResult ImportBatch::execute(const std::filesystem::path& folder, const std::string& archive_name,
                                const StoredFile& archive_file) {
    const auto candidates = files_.scan(folder);
    if (candidates.empty()) throw Error(ErrorCode::invalid_input);
    ImportResult result;
    result.archive_name = archive_name;
    result.archive_file = archive_file;
    result.batch.id = identity_.new_id();
    result.batch.created_at = identity_.now_utc();
    for (const auto& path : candidates) {
        const auto member = path.lexically_relative(folder).generic_u8string();
        const auto source = archive_name.empty() ? utf8(path) :
            archive_name + "!/" + std::string(member.begin(),member.end());
        try {
            auto stored = files_.preserve(path);
            const auto name = utf8(path.filename());
            result.documents.push_back({identity_.new_id(), result.batch.id, name,
                normalize_filename(name), source, identity_.now_utc(), std::move(stored), false});
        } catch (const Error& error) {
            if (error.code() != ErrorCode::file_io && error.code() != ErrorCode::integrity &&
                error.code() != ErrorCode::invalid_input) throw;
            result.issues.push_back({identity_.new_id(), result.batch.id, source, error.what()});
        }
    }
    result.batch.imported_count = result.documents.size();
    result.batch.issue_count = result.issues.size();
    if (!result.issues.empty()) result.batch.status = "needs_review";
    repository_.save(result);
    return result;
}
} // namespace muz
