#pragma once
#include "muz/application/ports.hpp"
#include <QSqlDatabase>

namespace muz {
class SqliteRepository final : public BatchRepository {
public:
    explicit SqliteRepository(const std::filesystem::path& database);
    ~SqliteRepository() override;
    void reset_for_testing();
    SqliteRepository(const SqliteRepository&) = delete;
    SqliteRepository& operator=(const SqliteRepository&) = delete;
    void save(ImportResult& result) override;
    std::vector<ProcessingBatch> batches() override;
    std::vector<IncomingDocument> documents(const std::string& batch_id) override;
    std::vector<ReviewIssue> issues(const std::string& batch_id) override;
    std::vector<AccountingRow> accounting_rows(const std::string& batch_id);
    void save_accounting(const std::string& batch_id, const std::vector<AccountingRow>& rows, bool review);
    void replace_accounting(const std::string& batch_id, const std::vector<AccountingRow>& rows);
    std::string record_export(const std::string& batch_id, const std::filesystem::path& path,
                       const std::string& sha256, std::size_t count, bool draft);
    void complete_export(const std::string& id, const std::string& batch_id, bool draft);
private:
    QSqlDatabase db_;
    void migrate();
};
} // namespace muz
