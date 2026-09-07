#pragma once

#include "muz/application/ports.hpp"
#include <memory>

namespace muz {
// Serial writes; each operation creates its SQLite connection on the calling thread.
class LocalWorkspace final : public Workspace {
public:
    explicit LocalWorkspace(std::filesystem::path root);
    ~LocalWorkspace() override;
    void reset_database_for_testing() override;
    LocalWorkspace(const LocalWorkspace&) = delete;
    LocalWorkspace& operator=(const LocalWorkspace&) = delete;
    ImportResult import_folder(const std::filesystem::path& folder) override;
    std::vector<ProcessingBatch> batches() override;
    std::vector<IncomingDocument> documents(const std::string& batch_id) override;
    std::vector<ReviewIssue> issues(const std::string& batch_id) override;
    ImportResult import_archive(const std::filesystem::path& archive) override;
    std::vector<AccountingRow> prepare_accounting(const std::string& batch_id) override;
    std::vector<AccountingRow> accounting_rows(const std::string& batch_id) override;
    void review_accounting(const std::string& batch_id, const std::vector<AccountingRow>& rows) override;
    void export_accounting(const std::string& batch_id, const std::filesystem::path& output, bool draft) override;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace muz
