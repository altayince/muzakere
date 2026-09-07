#pragma once
#include "muz/application/ports.hpp"

namespace muz {
class ImportBatch final {
public:
    ImportBatch(FileStore& files, BatchRepository& repository, IdentityClock& identity)
        : files_(files), repository_(repository), identity_(identity) {}
    ImportResult execute(const std::filesystem::path& folder, const std::string& archive_name = {},
                         const StoredFile& archive_file = {});
private:
    FileStore& files_;
    BatchRepository& repository_;
    IdentityClock& identity_;
};
} // namespace muz
