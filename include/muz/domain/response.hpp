#pragma once
#include "muz/domain/accounting.hpp"
#include <filesystem>

namespace muz {
struct ResponseProfile { std::string lawyer; std::string address; };
struct ResponseRow {
    std::string id;
    std::string return_id;
    AccountingRow source;
    std::optional<std::int64_t> available_cents;
    std::string accounting_input;
    std::vector<std::string> errors;
    bool demo{};
    std::string output_pdf;
};
struct AccountingReturn {
    std::string id;
    std::string source_name;
    std::string created_at;
    std::string sha256;
    std::string managed_path;
    std::vector<ResponseRow> rows;
};
struct ResponseExport {
    std::string id;
    std::string row_id;
    std::string path;
    std::string sha256;
    std::string template_sha256;
};
} // namespace muz
