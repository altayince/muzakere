#pragma once

#include "muz/domain/model.hpp"

namespace muz {
class FileStore {
public:
    virtual ~FileStore() = default;
    virtual std::vector<std::filesystem::path> scan(const std::filesystem::path& folder) = 0;
    virtual StoredFile preserve(const std::filesystem::path& source) = 0;
};

class BatchRepository {
public:
    virtual ~BatchRepository() = default;
    // Batch, documents, review issues and audit events commit together.
    virtual void save(ImportResult& result) = 0;
    virtual std::vector<ProcessingBatch> batches() = 0;
    virtual std::vector<IncomingDocument> documents(const std::string& batch_id) = 0;
    virtual std::vector<ReviewIssue> issues(const std::string& batch_id) = 0;
};

class IdentityClock {
public:
    virtual ~IdentityClock() = default;
    virtual std::string new_id() = 0;
    virtual std::string now_utc() = 0;
};

class Workspace {
public:
    virtual ~Workspace() = default;
    virtual ImportResult import_folder(const std::filesystem::path& folder) = 0;
    virtual std::vector<ProcessingBatch> batches() = 0;
    virtual std::vector<IncomingDocument> documents(const std::string& batch_id) = 0;
    virtual std::vector<ReviewIssue> issues(const std::string& batch_id) = 0;
};
} // namespace muz
