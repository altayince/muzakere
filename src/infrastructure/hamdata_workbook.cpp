#include "accounting_adapters.hpp"
#include "response_adapters.hpp"
#include "qt_paths.hpp"
#include <xlsxdocument.h>
#include <xlsxworksheet.h>
#include <xlsxcell.h>
#include <xlsxformat.h>
#include <QDateTime>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QSet>
#include <QRegularExpression>
#include <QUuid>
#include <cmath>

namespace muz {
namespace {
const QStringList headers={"Tebliğ Tarihi","Son Cevap Tarihi","ETebliğ (ET)/ Fiziki Posta (FP)/ KEP",
    "Konu","Muhatap","İCRA DAİRESİ","DosyaNo","Borç Miktarı","Borçlu","Borçlu T.C/Vergi No","Alacaklı"};
QString s(const std::string& value){return QString::fromStdString(value);}
std::string uuid(){return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();}
void check(bool value){if(!value)throw Error(ErrorCode::file_io);}
QString name_key(QString value){return value.toUpper().replace(QChar(0x130),'I').replace(QChar(0x131),'I').simplified();}
QString identity(const QVariant& value,bool numeric) {
    if(numeric) {
        bool ok=false;const auto number=value.toDouble(&ok);
        if(!ok || !std::isfinite(number) || number<0 || number>99999999999.0 || std::floor(number)!=number)return {};
        // Excel numeric cells drop leading VKN zeros. Never shorten an 11-digit TCKN.
        return QString::number(static_cast<qlonglong>(number)).rightJustified(10,'0');
    }
    return value.toString().trimmed();
}
bool valid_id(const QString& value){return QRegularExpression("^[0-9]{10,11}$").match(value).hasMatch();}
QString date_text(const QVariant& value) {
    if(value.metaType().id()==QMetaType::QDateTime)return value.toDateTime().date().toString("dd.MM.yyyy");
    if(value.metaType().id()==QMetaType::QDate)return value.toDate().toString("dd.MM.yyyy");
    return value.toString().trimmed();
}
QStringList fields(const AccountingRow& row) {
    QStringList result={s(row.cells[service_date]),{},{},row.cells[first_notice]=="Evet"?QString("89/1"):QString{},
        s(recipient_text(row)),s(row.cells[office]),s(row.cells[case_number]),s(row.cells[amount]),
        s(row.cells[debtor]),s(row.cells[debtor_id]),s(row.cells[creditor])};
    if(const auto value=parse_money(row.cells[amount]))result[7]=s(format_money(*value));
    for(auto& value:result)value=value.trimmed();
    return result;
}
QStringList visible_fields(QXlsx::Document& book,int r) {
    QStringList result;
    for(int c=1;c<=11;++c) {
        const auto value=book.read(r,c);
        if(c==1 || c==2)result.append(date_text(value));
        else if(c==10){const auto cell=book.cellAt(r,c);result.append(identity(value,cell && (cell->cellType()==QXlsx::Cell::NumberType || cell->cellType()==QXlsx::Cell::CustomType)));}
        else if(c==8) {
            auto raw=value.toString().trimmed();raw.remove(QRegularExpression("\\s*(TL|₺)\\s*$"));
            const auto cell=book.cellAt(r,c);bool invalid=false;
            const auto cents=cell && (cell->cellType()==QXlsx::Cell::NumberType || cell->cellType()==QXlsx::Cell::CustomType)?accounting_cents(cell.get(),invalid):parse_money(raw.toStdString());
            result.append(cents && !invalid?s(format_money(*cents)):raw);
        } else result.append(value.toString().trimmed());
    }
    return result;
}
QString fingerprint(const QStringList& values) {
    QJsonArray array;for(const auto& value:values)array.append(value);
    return QString::fromLatin1(QCryptographicHash::hash(QJsonDocument(array).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex());
}
void validate_sheet(QXlsx::Document& book,const QString& name,int max_columns) {
    if(!book.selectSheet(name) || book.dimension().lastRow()>10001 || book.dimension().lastColumn()>max_columns)
        throw Error(ErrorCode::invalid_input);
}
}

void write_hamdata_xlsx(const std::vector<AccountingRow>& rows,const std::filesystem::path& path,bool with_accounting) {
    QXlsx::Document book;
    check(book.addSheet(with_accounting?"MUHASEBE":"HAMDATA"));
    if(book.sheetNames().contains("Sheet1"))check(book.deleteSheet("Sheet1"));
    QXlsx::Format header,body,id,money,date;
    header.setFontBold(true);header.setPatternBackgroundColor(QColor("#FCE5CD"));header.setTextWrap(true);
    body.setVerticalAlignment(QXlsx::Format::AlignTop);body.setTextWrap(true);
    id=body;id.setNumberFormat("@");money=body;money.setNumberFormat("#,##0.00");date=body;date.setNumberFormat("dd.mm.yyyy");
    if(with_accounting) {
        check(book.write(1,1,"Borçlu T.C/Vergi No",header));check(book.write(1,2,"Borçlu",header));
        // Match the supplied workbook: A/B identity, C account reference, D returned amount.
        // Accountants can add further E/F account/amount pairs. All reply cells start empty.
        book.setColumnWidth(1,24.54);book.setColumnWidth(2,134.45);book.setColumnWidth(3,24);book.setColumnWidth(4,22);
        QSet<QString> seen;int r=2;
        for(const auto& row:rows) {
            const auto key=s(row.cells[debtor_id]).trimmed();
            // Missing/invalid identities remain in HAMDATA for review, never grouped by name.
            if(!valid_id(key) || seen.contains(key))continue;
            seen.insert(key);
            check(book.currentWorksheet()->writeString(r,1,key,id));
            check(book.currentWorksheet()->writeString(r,2,s(row.cells[debtor]),body));
            check(book.currentWorksheet()->writeString(r,3,QString{},id));check(book.currentWorksheet()->writeString(r,4,QString{},money));++r;
        }
        check(book.addSheet("HAMDATA"));
    }
    const std::array<double,11> widths={16.09,20.18,14.82,12.09,66.09,80.54,28.82,26.27,65.73,24.91,94.45};
    for(int c=1;c<=11;++c){check(book.write(1,c,headers[c-1],header));book.setColumnWidth(c,widths[static_cast<std::size_t>(c-1)]);}
    book.setRowHeight(1,46);int r=2;
    for(const auto& row:rows) {
        const auto values=fields(row);
        for(int c=1;c<=11;++c) {
            if(c==1 && QDate::fromString(values[0],"dd.MM.yyyy").isValid())check(book.write(r,c,QDate::fromString(values[0],"dd.MM.yyyy"),date));
            else if(c==8 && parse_money(row.cells[amount]))check(book.write(r,c,static_cast<double>(*parse_money(row.cells[amount]))/100.0,money));
            else check(book.currentWorksheet()->writeString(r,c,values[c-1],c==7 || c==10?id:body));
        }
        book.setRowHeight(r,45);++r;
    }
    // Preserve source blockers without adding columns to the real office layout.
    check(book.addSheet("_MUZ"));check(book.write(1,1,"MUZ-HAMDATA-1"));r=2;
    for(const auto& row:rows) {
        auto snapshot=row;snapshot.source_text.clear();snapshot.evidence.clear();
        check(book.write(r,1,fingerprint(fields(row))));check(book.write(r,2,encode_row(snapshot)));++r;
    }
    book.currentWorksheet()->setHidden(true);
    book.selectSheet(with_accounting?"MUHASEBE":"HAMDATA");check(book.saveAs(qpath(path)));
}

AccountingReturn read_hamdata_return(QXlsx::Document& book) {
    AccountingReturn result;result.id=uuid();result.created_at=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toStdString();
    QMap<QString,QList<AccountingRow>> snapshots;
    const bool generated=book.sheetNames().contains("_MUZ");
    if(generated) {
        validate_sheet(book,"_MUZ",2);if(book.read(1,1).toString()!="MUZ-HAMDATA-1")throw Error(ErrorCode::invalid_input);
        for(int r=2;r<=book.dimension().lastRow();++r) {
            const auto payload=book.read(r,2).toString();if(payload.size()>200000)throw Error(ErrorCode::invalid_input);
            snapshots[book.read(r,1).toString()].append(decode_row(payload));
        }
    }
    struct Reply {std::optional<std::int64_t> cents;QString input;QString name;QStringList errors;};
    QMap<QString,Reply> replies;
    validate_sheet(book,"MUHASEBE",100);
    if(book.read(1,1).toString()!="Borçlu T.C/Vergi No" || book.read(1,2).toString()!="Borçlu")throw Error(ErrorCode::invalid_input);
    const int columns=std::max(4,book.dimension().lastColumn());
    for(int r=2;r<=book.dimension().lastRow();++r) {
        bool empty=true;for(int c=1;c<=columns;++c)if(!book.read(r,c).toString().trimmed().isEmpty())empty=false;
        if(empty)continue;
        const auto id_cell=book.cellAt(r,1);
        const auto key=identity(book.read(r,1),id_cell && (id_cell->cellType()==QXlsx::Cell::NumberType || id_cell->cellType()==QXlsx::Cell::CustomType));
        if(!valid_id(key))throw Error(ErrorCode::invalid_input);
        Reply reply;reply.name=book.read(r,2).toString();
        for(int c=1;c<=2;++c)if(auto cell=book.cellAt(r,c);cell && cell->hasFormula())reply.errors.append("Muhasebe kimlik alanlarında formül var");
        int amounts=0;
        for(int c=4;c<=columns;c+=2) {
            bool invalid=false;const auto cell=book.cellAt(r,c);const auto amount=accounting_cents(cell.get(),invalid);
            if(invalid)reply.errors.append("Muhasebe tutarı geçersiz; sayı veya boş hücre olmalı");
            if(amount){++amounts;reply.cents=amount;reply.input=cell->value().toString();}
        }
        // Multiple account balances need an explicit aggregation rule; never choose silently.
        if(amounts>1)reply.errors.append("Birden fazla hesap tutarı var; toplam/son tutar seçimi incelenmeli");
        if(replies.contains(key)){replies[key].errors.append("Muhasebe sayfasında yinelenen T.C./vergi numarası");continue;}
        replies.insert(key,reply);
    }
    validate_sheet(book,"HAMDATA",11);
    for(int c=1;c<=11;++c)if(book.read(1,c).toString()!=headers[c-1])throw Error(ErrorCode::invalid_input);
    for(int r=2;r<=book.dimension().lastRow();++r) {
        const auto values=visible_fields(book,r);bool empty=true;for(const auto& value:values)if(!value.isEmpty())empty=false;
        if(empty)continue;
        ResponseRow response;response.id=uuid();response.return_id=result.id;
        auto& row=response.source;row.id=uuid();row.document_id=row.id;
        row.cells={values[0].toStdString(),values[5].toStdString(),values[6].toStdString(),values[7].toStdString(),
            values[8].toStdString(),values[9].toStdString(),values[10].toStdString(),{},values[3]=="89/1"?"Evet":"Hayır",{}};
        row.recipient=values[4].toStdString();row.approved=true;
        if(generated) {
            const auto key=fingerprint(values);
            if(!snapshots.contains(key) || snapshots[key].empty())response.errors.push_back("HAMDATA kaynak alanları değiştirilmiş veya satır eklenmiş");
            else row=snapshots[key].takeFirst();
        }
        for(int c=1;c<=11;++c)if(auto cell=book.cellAt(r,c);cell && cell->hasFormula())response.errors.push_back("HAMDATA alanlarında formül var");
        const auto key=values[9];
        if(!valid_id(key) || !replies.contains(key))response.errors.push_back("Borçlu için tekil muhasebe eşleşmesi bulunamadı; boş tutar sayılmaz");
        else {
            const auto& reply=replies[key];response.available_cents=reply.cents;response.accounting_input=reply.input.toStdString();
            for(const auto& error:reply.errors)response.errors.push_back(error.toStdString());
            if(name_key(values[8])!=name_key(reply.name))row.warnings.push_back("Muhasebedeki ad farklı; eşleşme T.C./vergi numarasıyla yapıldı");
        }
        if(!can_approve(row))response.errors.push_back("Dosya bilgilerinde eksik veya çelişkili alan var");
        if(row.cells[first_notice]!="Evet")response.errors.push_back("Bu şablon yalnızca 89/1 içindir");
        if(row.recipient.empty())response.errors.push_back("Muhatap eksik");
        result.rows.push_back(std::move(response));
    }
    if(result.rows.empty())throw Error(ErrorCode::invalid_input);
    if(generated)for(const auto& remaining:snapshots)if(!remaining.empty())throw Error(ErrorCode::invalid_input);
    return result;
}
} // namespace muz
