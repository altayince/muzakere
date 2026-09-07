#pragma once

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace muz {

// UTF-8 at application boundaries; paths retain the native filesystem encoding.
enum class ErrorCode { invalid_input, file_io, integrity, storage, schema_version, workspace_busy };
class Error final : public std::runtime_error {
public:
    explicit Error(ErrorCode code);
    [[nodiscard]] ErrorCode code() const noexcept { return code_; }
private:
    ErrorCode code_;
};

[[nodiscard]] std::string normalize_filename(std::string name);
[[nodiscard]] bool valid_sha256(const std::string& hash);

struct StoredFile {
    std::string sha256;
    std::string managed_path; // Workspace-relative, content-addressed path.
    std::uint64_t size{};
    std::string mime_type;
};

struct IncomingDocument {
    std::string id;
    std::string batch_id;
    std::string original_filename;
    std::string normalized_filename;
    std::string source_path;
    std::string imported_at;
    StoredFile file;
    bool duplicate{}; // Same bytes, never an inferred legal case relationship.
};

struct ReviewIssue {
    std::string id;
    std::string batch_id;
    std::string source_path;
    std::string code;
};

struct ProcessingBatch {
    std::string id;
    std::string created_at;
    std::string status{"imported"};
    std::uint64_t imported_count{};
    std::uint64_t duplicate_count{};
    std::uint64_t issue_count{};
};

struct ImportResult {
    ProcessingBatch batch;
    std::vector<IncomingDocument> documents;
    std::vector<ReviewIssue> issues;
};

// Populated only by a future explicit review/approval use case.
struct CaseRecord {
    std::string id;
    std::string external_case_number;
    std::string court_name;
};

} // namespace muz
