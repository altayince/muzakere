#pragma once
#include "muz/domain/response.hpp"
#include <QString>
#include <QByteArray>
namespace QXlsx { class Document; class Cell; }

namespace muz {
AccountingReturn read_accounting_return(const QByteArray& workbook);
AccountingReturn read_hamdata_return(QXlsx::Document& workbook);
std::optional<std::int64_t> accounting_cents(const QXlsx::Cell* cell, bool& invalid);
QString encode_response(const ResponseRow& row);
ResponseRow decode_response(const QString& payload);
QString response_html(const ResponseRow& row, const ResponseProfile& profile);
void write_response_pdf(const QString& html, const std::filesystem::path& output);
} // namespace muz
