#include "response_adapters.hpp"
#include "accounting_adapters.hpp"
#include "muz/domain/model.hpp"
#include <xlsxdocument.h>
#include <xlsxworksheet.h>
#include <xlsxcell.h>
#include <QBuffer>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUuid>
#include <QDateTime>
#include <QSet>
#include <QMap>
#include <cmath>
#include "qt_paths.hpp"

namespace muz {
namespace {
QString text(const std::string& value){return QString::fromStdString(value);}
std::string uuid(){return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();}
std::optional<std::int64_t> numeric_cents(const QXlsx::Cell* cell, bool& invalid) {
    invalid=false;
    if(!cell)return {};
    if(cell->hasFormula() || cell->isDateTime() || cell->cellType()==QXlsx::Cell::BooleanType ||
       cell->cellType()==QXlsx::Cell::ErrorType){invalid=true;return {};}
    const auto value=cell->value();
    if(!value.isValid() || value.toString().trimmed().isEmpty())return {};
    if(cell->cellType()==QXlsx::Cell::NumberType) {
        bool ok=false; const double amount=value.toDouble(&ok), rounded=std::round(amount*100.0);
        if(!ok || !std::isfinite(amount) || std::abs(rounded)>900000000000000.0 ||
           std::abs(amount*100.0-rounded)>0.001){invalid=true;return {};}
        return static_cast<std::int64_t>(rounded);
    }
    auto raw=value.toString().trimmed(); bool negative=raw.startsWith('-');
    if(negative || raw.startsWith('+'))raw.remove(0,1);
    if(!raw.contains(','))raw+=",00";
    const auto parsed=parse_money(raw.toStdString());
    if(!parsed){invalid=true;return {};}
    return negative?-*parsed:*parsed;
}
}

QString encode_response(const ResponseRow& row) {
    QJsonArray errors; for(const auto& value:row.errors)errors.append(text(value));
    QJsonObject object{{"id",text(row.id)},{"return_id",text(row.return_id)},
        {"source",encode_row(row.source)},{"input",text(row.accounting_input)},{"errors",errors},{"demo",row.demo}};
    if(row.available_cents)object["cents"]=QString::number(*row.available_cents);
    return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}
ResponseRow decode_response(const QString& payload) {
    QJsonParseError error; const auto document=QJsonDocument::fromJson(payload.toUtf8(),&error);
    if(error.error!=QJsonParseError::NoError || !document.isObject())throw Error(ErrorCode::storage);
    const auto o=document.object(); ResponseRow row;
    row.id=o["id"].toString().toStdString(); row.return_id=o["return_id"].toString().toStdString();
    row.source=decode_row(o["source"].toString()); row.accounting_input=o["input"].toString().toStdString(); row.demo=o["demo"].toBool();
    if(o.contains("cents")){bool ok=false;row.available_cents=o["cents"].toString().toLongLong(&ok);if(!ok)throw Error(ErrorCode::storage);}
    for(const auto& value:o["errors"].toArray())row.errors.push_back(value.toString().toStdString());
    return row;
}

AccountingReturn read_accounting_return(const QByteArray& bytes) {
    if(bytes.isEmpty() || bytes.size()>16*1024*1024)throw Error(ErrorCode::invalid_input);
    QTemporaryDir validated; if(!validated.isValid())throw Error(ErrorCode::file_io);
    extract_zip(bytes,native_path(validated.path()));
    QBuffer buffer;buffer.setData(bytes);buffer.open(QIODevice::ReadOnly);
    QXlsx::Document book(&buffer);if(!book.load() || !book.selectSheet("Kaynaklar"))throw Error(ErrorCode::invalid_input);
    struct Snapshot {AccountingRow row;bool draft{};bool demo{};};
    QMap<int,Snapshot> snapshots;QSet<QString> ids;
    const int metadata_end=book.dimension().lastRow();
    if(metadata_end<2 || metadata_end>10001)throw Error(ErrorCode::invalid_input);
    for(int r=2;r<=metadata_end;++r) {
        for(int c=1;c<=14;++c)if(auto cell=book.cellAt(r,c);cell && cell->hasFormula())throw Error(ErrorCode::invalid_input);
        if(book.read(r,14).toString()!="MUZ-RETURN-1")throw Error(ErrorCode::invalid_input);
        bool ok=false;const int sequence=book.read(r,1).toString().toInt(&ok);
        const auto payload=book.read(r,11).toString();if(payload.size()>200000)throw Error(ErrorCode::invalid_input);
        const auto source=decode_row(payload);
        if(!ok || sequence<1 || snapshots.contains(sequence) || source.id.empty() || ids.contains(text(source.id)) ||
            book.read(r,2).toString()!=text(source.id))throw Error(ErrorCode::invalid_input);
        ids.insert(text(source.id));snapshots.insert(sequence,{source,book.read(r,12).toString()=="1",book.read(r,13).toString()=="1"});
    }
    if(!book.selectSheet("İcra Dosyaları"))throw Error(ErrorCode::invalid_input);
    const QStringList headers={"Sıra No","Tebliğ Tarihi","İcra Dairesi","Esas Numarası","Borç Miktarı (TL)",
        "Borçlu","Borçlu TCKN/VKN","Alacaklı","İcra Dairesi İBAN","89/1 Haciz İhbarnamesi mi?","Açıklama","Muhatap","Uyarılar","Muhasebe"};
    QMap<QString,int> columns;
    if(book.dimension().lastColumn()>100 || book.dimension().lastRow()>10001)throw Error(ErrorCode::invalid_input);
    for(int c=1;c<=book.dimension().lastColumn();++c) {
        const auto name=book.read(1,c).toString();if(name.isEmpty())continue;
        if(columns.contains(name))throw Error(ErrorCode::invalid_input);
        columns.insert(name,c);
    }
    for(const auto& header:headers)if(!columns.contains(header))throw Error(ErrorCode::invalid_input);
    AccountingReturn result;result.id=uuid();result.created_at=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
    QSet<int> used;
    for(int r=2;r<=book.dimension().lastRow();++r) {
        bool empty=true;for(const auto& header:headers)if(!book.read(r,columns[header]).toString().trimmed().isEmpty())empty=false;
        if(empty)continue;
        bool ok=false;const int sequence=book.read(r,columns["Sıra No"]).toString().toInt(&ok);
        if(!ok || !snapshots.contains(sequence) || used.contains(sequence))throw Error(ErrorCode::invalid_input);
        used.insert(sequence);const auto& snapshot=snapshots[sequence];
        ResponseRow row;row.id=uuid();row.return_id=result.id;row.source=snapshot.row;row.demo=snapshot.demo;
        bool changed=false;
        for(int c=0;c<headers.size()-1;++c) {
            const auto cell=book.cellAt(r,columns[headers[c]]);
            if(cell && cell->hasFormula()){changed=true;continue;}
            if(c==0 || c==12)continue; // Warnings are explanatory, sequence is the stable row key.
            QString expected;
            if(c==11)expected=text(recipient_text(row.source));
            else expected=text(row.source.cells[static_cast<std::size_t>(c-1)]);
            if(c==10 && snapshot.draft)expected="İNCELEME TASLAĞI — "+expected;
            if(c==4 && parse_money(row.source.cells[amount])) {
                bool invalid=false; const auto actual=numeric_cents(cell.get(),invalid);
                if(invalid || actual!=parse_money(row.source.cells[amount]))changed=true;
            } else if(book.read(r,columns[headers[c]]).toString()!=expected)changed=true;
        }
        if(changed)row.errors.push_back("Dosya/borçlu alanları gönderilen Excel ile uyuşmuyor; yalnızca Muhasebe sütununu değiştirin");
        const auto accounting=book.cellAt(r,columns["Muhasebe"]);
        bool invalid=false;row.available_cents=numeric_cents(accounting.get(),invalid);
        row.accounting_input=accounting?accounting->value().toString().toStdString():std::string{};
        if(invalid)row.errors.push_back("Muhasebe hücresi boş veya en çok iki ondalıklı sayı olmalı; formül/tarih/metin kabul edilmez");
        if(!row.source.approved && !row.demo)row.errors.push_back("Muhasebeye onaylı Excel gönderilmeli; bu satır onaylanmamış");
        if(!can_approve(row.source))row.errors.push_back("Kaynak satırda onayı engelleyen eksik veya çelişkili bilgi var");
        if(row.source.cells[first_notice]!="Evet")row.errors.push_back("VAR/YOK şablonları yalnızca birinci haciz ihbarnamesi (89/1) içindir");
        if(row.source.recipient.empty())row.errors.push_back("Muhatap eksik");
        result.rows.push_back(std::move(row));
    }
    if(result.rows.empty() || used.size()!=snapshots.size())throw Error(ErrorCode::invalid_input);
    return result;
}
} // namespace muz
