#include "muz/domain/accounting.hpp"
#include <algorithm>
#include <charconv>
#include <limits>

namespace muz {
std::optional<std::int64_t> parse_money(const std::string& value) {
    // Public/editable representation is Turkish decimal: 1.234,56 or 1234,56.
    const auto comma = value.find(',');
    if (comma == std::string::npos || value.size() - comma != 3) return {};
    std::string digits;
    const auto integer = value.substr(0, comma);
    if (integer.empty()) return {};
    const auto dot = integer.find('.');
    if (dot != std::string::npos) {
        if (dot == 0 || dot > 3) return {};
        for (std::size_t i = dot; i < integer.size(); i += 4)
            if (integer[i] != '.' || i + 4 > integer.size()) return {};
    }
    for (std::size_t i = 0; i < value.size(); ++i) {
        const char c = value[i];
        if (c >= '0' && c <= '9') digits += c;
        else if (!(i < comma && c == '.') && i != comma) return {};
    }
    std::int64_t result{};
    const auto parsed = std::from_chars(digits.data(), digits.data() + digits.size(), result);
    if (parsed.ec != std::errc{} || parsed.ptr != digits.data() + digits.size() || result > 900000000000000LL) return {};
    return result;
}
std::string format_money(std::int64_t cents) {
    return std::to_string(cents / 100) + ',' + (cents % 100 < 10 ? "0" : "") + std::to_string(cents % 100);
}
bool can_approve(const AccountingRow& row) {
    if(row.pair_conflict)return false;
    const auto& number=row.cells[case_number];
    if(number.size()<6 || number[4]!='/' || !std::all_of(number.begin(),number.begin()+4,[](char c){return c>='0'&&c<='9';}) ||
       !std::all_of(number.begin()+5,number.end(),[](char c){return c>='0'&&c<='9';})) return false;
    return !row.cells[office].empty() && !row.cells[case_number].empty() && !row.cells[debtor].empty() &&
        (row.cells[first_notice] == "Evet" || row.cells[first_notice] == "Hayır") &&
        (row.cells[amount].empty() || parse_money(row.cells[amount]).has_value());
}
} // namespace muz
