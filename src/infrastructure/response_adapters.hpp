#pragma once
#include "muz/domain/response.hpp"
#include <QString>
#include <QByteArray>

namespace muz {
AccountingReturn read_accounting_return(const QByteArray& workbook);
QString encode_response(const ResponseRow& row);
ResponseRow decode_response(const QString& payload);
QString response_html(const ResponseRow& row, const ResponseProfile& profile);
void write_response_pdf(const QString& html, const std::filesystem::path& output);
} // namespace muz
