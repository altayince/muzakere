#pragma once
#include <array>
#include <optional>
#include <string>
#include <vector>
#include <cstdint>

namespace muz {
// Columns B..K of the user's accounting format, encoded as UTF-8.
enum Column : std::size_t { service_date, office, case_number, amount, debtor,
    debtor_id, creditor, iban, first_notice, notes, column_count };
struct FieldEvidence {
    std::size_t column{};
    std::string document_id;
    std::string snippet;
    std::string method;
    double confidence{};
};
struct AccountingRow {
    std::string id;
    std::string batch_id;
    std::string document_id;
    std::string envelope_id;
    std::string source_path;
    std::string sha256;
    std::string source_text;
    std::string recipient;
    std::string conflicting_recipient;
    int debtor_index{};
    bool identity_conflict{};
    bool pair_conflict{};
    double match_confidence{};
    std::array<std::string, column_count> cells;
    std::vector<std::string> warnings;
    std::vector<FieldEvidence> evidence;
    bool approved{};
};
[[nodiscard]] std::optional<std::int64_t> parse_money(const std::string& value);
[[nodiscard]] std::string format_money(std::int64_t cents);
[[nodiscard]] bool can_approve(const AccountingRow& row);
enum class ReviewStatus { ready, review, blocked };
[[nodiscard]] ReviewStatus review_status(const AccountingRow& row);
[[nodiscard]] std::string recipient_text(const AccountingRow& row);
} // namespace muz
