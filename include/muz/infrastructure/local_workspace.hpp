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
    void export_demo_accounting(const std::string& batch_id, const std::filesystem::path& output) override;
    void export_hamdata(const std::string& batch_id, const std::filesystem::path& output, bool with_accounting) override;
    AccountingReturn import_accounting_return(const std::filesystem::path& input) override;
    std::vector<AccountingReturn> accounting_returns() override;
    std::vector<ResponseRow> response_rows(const std::string& return_id) override;
    ResponseProfile response_profile() override;
    void save_response_profile(const ResponseProfile& profile) override;
    std::string preview_response(const ResponseRow& row, const ResponseProfile& profile) override;
    std::filesystem::path generate_responses(const std::string& return_id, const std::vector<std::string>& row_ids,
        const std::filesystem::path& output, const ResponseProfile& profile) override;
private:
    void export_accounting_impl(const std::string& batch_id, const std::filesystem::path& output, bool draft, bool demo, int hamdata_mode = 0);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace muz
