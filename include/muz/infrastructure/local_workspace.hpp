#pragma once

#include "muz/application/ports.hpp"
#include <memory>

namespace muz {
// Serial writes; each operation creates its SQLite connection on the calling thread.
class LocalWorkspace final : public Workspace {
public:
    explicit LocalWorkspace(std::filesystem::path root);
    ~LocalWorkspace() override;
    LocalWorkspace(const LocalWorkspace&) = delete;
    LocalWorkspace& operator=(const LocalWorkspace&) = delete;
    ImportResult import_folder(const std::filesystem::path& folder) override;
    std::vector<ProcessingBatch> batches() override;
    std::vector<IncomingDocument> documents(const std::string& batch_id) override;
    std::vector<ReviewIssue> issues(const std::string& batch_id) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace muz
