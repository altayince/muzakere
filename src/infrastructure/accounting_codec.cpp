#include "accounting_adapters.hpp"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>

namespace muz {
QString encode_row(const AccountingRow& row) {
    QJsonObject object;
    object["id"] = QString::fromStdString(row.id);
    object["batch_id"] = QString::fromStdString(row.batch_id);
    object["document_id"] = QString::fromStdString(row.document_id);
    object["envelope_id"] = QString::fromStdString(row.envelope_id);
    object["source_path"] = QString::fromStdString(row.source_path);
    object["sha256"] = QString::fromStdString(row.sha256);
    object["source_text"] = QString::fromStdString(row.source_text);
    object["recipient"] = QString::fromStdString(row.recipient);
    object["pair_conflict"] = row.pair_conflict;
    object["match_confidence"] = row.match_confidence;
    object["approved"] = row.approved;
    QJsonArray cells, warnings, evidence;
    for (const auto& cell : row.cells) cells.append(QString::fromStdString(cell));
    for (const auto& warning : row.warnings) warnings.append(QString::fromStdString(warning));
    for (const auto& field : row.evidence) evidence.append(QJsonObject{
        {"column", static_cast<int>(field.column)}, {"document_id", QString::fromStdString(field.document_id)},
        {"snippet", QString::fromStdString(field.snippet)}, {"method", QString::fromStdString(field.method)},
        {"confidence", field.confidence}});
    object["cells"] = cells; object["warnings"] = warnings; object["evidence"] = evidence;
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}
AccountingRow decode_row(const QString& payload) {
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(payload.toUtf8(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) throw Error(ErrorCode::storage);
    const auto o = document.object();
    AccountingRow row;
    row.id = o["id"].toString().toStdString();
    row.batch_id = o["batch_id"].toString().toStdString();
    row.document_id = o["document_id"].toString().toStdString();
    row.envelope_id = o["envelope_id"].toString().toStdString();
    row.source_path = o["source_path"].toString().toStdString();
    row.sha256 = o["sha256"].toString().toStdString();
    row.source_text = o["source_text"].toString().toStdString();
    row.recipient = o["recipient"].toString().toStdString();
    row.pair_conflict = o["pair_conflict"].toBool();
    row.match_confidence = o["match_confidence"].toDouble();
    row.approved = o["approved"].toBool();
    const auto cells = o["cells"].toArray();
    if (cells.size() != column_count) throw Error(ErrorCode::storage);
    for (std::size_t i = 0; i < column_count; ++i) row.cells[i] = cells[static_cast<qsizetype>(i)].toString().toStdString();
    for (const auto& w : o["warnings"].toArray()) row.warnings.push_back(w.toString().toStdString());
    for (const auto& e : o["evidence"].toArray()) {
        const auto field = e.toObject();
        row.evidence.push_back({static_cast<std::size_t>(field["column"].toInt()),
            field["document_id"].toString().toStdString(), field["snippet"].toString().toStdString(),
            field["method"].toString().toStdString(), field["confidence"].toDouble()});
    }
    return row;
}
} // namespace muz
