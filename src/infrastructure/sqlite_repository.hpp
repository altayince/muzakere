#pragma once
#include "muz/application/ports.hpp"
#include <QSqlDatabase>

namespace muz {
class SqliteRepository final : public BatchRepository {
public:
    explicit SqliteRepository(const std::filesystem::path& database);
    ~SqliteRepository() override;
    SqliteRepository(const SqliteRepository&) = delete;
    SqliteRepository& operator=(const SqliteRepository&) = delete;
    void save(ImportResult& result) override;
    std::vector<ProcessingBatch> batches() override;
    std::vector<IncomingDocument> documents(const std::string& batch_id) override;
    std::vector<ReviewIssue> issues(const std::string& batch_id) override;
private:
    QSqlDatabase db_;
    void migrate();
};
} // namespace muz
