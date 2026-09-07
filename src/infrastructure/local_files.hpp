#pragma once
#include "muz/application/ports.hpp"

namespace muz {
class LocalFiles final : public FileStore {
public:
    explicit LocalFiles(std::filesystem::path root) : root_(std::move(root)) {}
    std::vector<std::filesystem::path> scan(const std::filesystem::path& folder) override;
    StoredFile preserve(const std::filesystem::path& source) override;
private:
    std::filesystem::path root_;
};
} // namespace muz
