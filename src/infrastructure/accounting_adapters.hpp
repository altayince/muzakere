#pragma once
#include "muz/domain/accounting.hpp"
#include "muz/domain/model.hpp"
#include <QString>
#include <QByteArray>

namespace muz {
struct PdfText { QString text; std::string error; };
struct ParsedDocument {
    IncomingDocument document;
    PdfText text;
    bool envelope{};
    AccountingRow row;
};
PdfText extract_pdf(const QByteArray& bytes);
ParsedDocument parse_document(const IncomingDocument& document, const PdfText& text);
std::vector<AccountingRow> match_accounting(std::vector<ParsedDocument> documents);
void extract_zip(const QByteArray& bytes, const std::filesystem::path& destination);
void write_accounting_xlsx(const std::vector<AccountingRow>& rows,
                           const std::filesystem::path& path, bool draft);
QString encode_row(const AccountingRow& row);
AccountingRow decode_row(const QString& payload);
} // namespace muz
