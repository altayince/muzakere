#include "accounting_adapters.hpp"
#include "qt_paths.hpp"
#include <xlsxdocument.h>
#include <xlsxworksheet.h>
#include <xlsxformat.h>
#include <QBuffer>
#include <QFile>
#include <QXmlStreamReader>
#include <QXmlStreamWriter>
#include <miniz.h>

namespace muz {
namespace {
void check(bool result) { if (!result) throw Error(ErrorCode::file_io); }
QString s(const std::string& value) { return QString::fromStdString(value); }

QByteArray worksheet_view(const QByteArray& input, int last_row) {
    QXmlStreamReader reader(input);
    QByteArray result;
    QXmlStreamWriter writer(&result);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == u"sheetViews") {
            writer.writeStartElement("sheetViews"); writer.writeStartElement("sheetView");
            writer.writeAttribute("workbookViewId","0");
            writer.writeEmptyElement("pane"); writer.writeAttribute("ySplit","1");
            writer.writeAttribute("topLeftCell","A2"); writer.writeAttribute("activePane","bottomLeft");
            writer.writeAttribute("state","frozen");
            writer.writeEndElement(); writer.writeEndElement(); reader.skipCurrentElement();
        } else {
            writer.writeCurrentToken(reader);
            if (reader.isEndElement() && reader.name() == u"sheetData") {
                writer.writeEmptyElement("autoFilter"); writer.writeAttribute("ref","A1:K" + QString::number(last_row));
            }
        }
    }
    if (reader.hasError()) throw Error(ErrorCode::storage);
    return result;
}

void publish_workbook(const QByteArray& workbook, const std::filesystem::path& path, int last_row) {
    mz_zip_archive source{}, target{};
    check(mz_zip_reader_init_mem(&source,workbook.constData(),static_cast<size_t>(workbook.size()),0));
    struct ReaderGuard { mz_zip_archive* zip; ~ReaderGuard(){mz_zip_reader_end(zip);} } source_guard{&source};
    check(mz_zip_writer_init_heap(&target,0,0));
    struct WriterGuard { mz_zip_archive* zip; ~WriterGuard(){mz_zip_writer_end(zip);} } target_guard{&target};
    for (mz_uint i=0;i<mz_zip_reader_get_num_files(&source);++i) {
        mz_zip_archive_file_stat stat{};
        check(mz_zip_reader_file_stat(&source,i,&stat));
        if (QString::fromUtf8(stat.m_filename)=="xl/worksheets/sheet1.xml") {
            QByteArray xml(static_cast<qsizetype>(stat.m_uncomp_size),Qt::Uninitialized);
            check(mz_zip_reader_extract_to_mem(&source,i,xml.data(),static_cast<size_t>(xml.size()),0));
            const auto changed=worksheet_view(xml,last_row);
            check(mz_zip_writer_add_mem(&target,stat.m_filename,changed.constData(),static_cast<size_t>(changed.size()),MZ_DEFAULT_COMPRESSION));
        } else check(mz_zip_writer_add_from_zip_reader(&target,&source,i));
    }
    void* output=nullptr; size_t size=0;
    check(mz_zip_writer_finalize_heap_archive(&target,&output,&size));
    struct BufferGuard { void* data; ~BufferGuard(){mz_free(data);} } output_guard{output};
    QFile file(qpath(path));
    check(file.open(QIODevice::WriteOnly));
    check(file.write(static_cast<const char*>(output),static_cast<qint64>(size))==static_cast<qint64>(size));
    check(file.flush());
}
}

void write_accounting_xlsx(const std::vector<AccountingRow>& rows, const std::filesystem::path& path, bool draft) {
    QXlsx::Document workbook;
    workbook.renameSheet("Sheet1","İcra Dosyaları");
    if (!workbook.selectSheet("İcra Dosyaları")) check(workbook.addSheet("İcra Dosyaları"));
    QXlsx::Format header, body, identifier, money;
    header.setFontBold(true); header.setFontColor(Qt::white);
    header.setPatternBackgroundColor(QColor("#1F4E78"));
    header.setTextWrap(true); header.setVerticalAlignment(QXlsx::Format::AlignVCenter);
    body.setTextWrap(true); body.setVerticalAlignment(QXlsx::Format::AlignTop);
    body.setBorderStyle(QXlsx::Format::BorderThin); body.setBorderColor(QColor("#D9E1F2"));
    identifier=body; identifier.setNumberFormat("@");
    money=body; money.setNumberFormat("#,##0.00");
    const QStringList headers={"Sıra No","Tebliğ Tarihi","İcra Dairesi","Esas Numarası","Borç Miktarı (TL)",
        "Borçlu","Borçlu TCKN/VKN","Alacaklı","İcra Dairesi İBAN","89/1 Haciz İhbarnamesi mi?","Açıklama"};
    const std::array<double,11> widths={9,16,48,18,21,38,27,48,32,24,70};
    for (int column=1;column<=headers.size();++column) {
        check(workbook.currentWorksheet()->writeString(1,column,headers[column-1],header));
        workbook.setColumnWidth(column,widths[static_cast<std::size_t>(column-1)]);
    }
    workbook.setRowHeight(1,34);
    int index=2;
    for (const auto& row:rows) {
        check(workbook.write(index,1,index-1,body));
        for (std::size_t column=0;column<column_count;++column) {
            auto value=s(row.cells[column]);
            if (column==notes) {
                if(!row.recipient.empty())value+=" | Muhatap: "+s(row.recipient);
                QStringList warnings;
                for (const auto& warning:row.warnings) warnings.append(s(warning));
                if (!warnings.empty()) value += " | " + warnings.join("; ");
                if (draft) value = "İNCELEME TASLAĞI — " + value;
            }
            if (column==amount && parse_money(row.cells[column])) {
                check(workbook.write(index,static_cast<int>(column)+2,
                    static_cast<double>(*parse_money(row.cells[column]))/100.0,money));
            } else {
                // Explicit strings: debtor names and notes cannot become spreadsheet formulas.
                check(workbook.currentWorksheet()->writeString(index,static_cast<int>(column)+2,value,
                    column==debtor_id || column==iban || column==case_number ? identifier:body));
            }
        }
        workbook.setRowHeight(index,75); ++index;
    }
    check(workbook.addSheet("Kaynaklar"));
    const QStringList sources={"Sıra No","Record ID","Batch ID","Belge ID","Zarf ID","Kaynak dosya","SHA-256","Eşleşme güveni","Onay durumu","Muhatap"};
    for (int c=0;c<sources.size();++c) check(workbook.currentWorksheet()->writeString(1,c+1,sources[c],header));
    index=2;
    for (const auto& row:rows) {
        const QStringList values={QString::number(index-1),s(row.id),s(row.batch_id),s(row.document_id),s(row.envelope_id),
            s(row.source_path),s(row.sha256),QString::number(row.match_confidence,'f',2),row.approved?"Onaylı":"Onay bekliyor",s(row.recipient)};
        for (int c=0;c<values.size();++c) check(workbook.currentWorksheet()->writeString(index,c+1,values[c],identifier));
        ++index;
    }
    workbook.setColumnWidth(1,9,35); workbook.setColumnWidth(6,85); workbook.setColumnWidth(7,70);
    check(workbook.addSheet("İnceleme"));
    const QStringList review_headers={"Record ID","Alan","Kaynak belge ID","Yöntem","Güven","Kaynak metin / kullanıcı girişi"};
    for (int c=0;c<review_headers.size();++c) check(workbook.currentWorksheet()->writeString(1,c+1,review_headers[c],header));
    index=2;
    for (const auto& row:rows) for (const auto& evidence:row.evidence) {
        const QStringList values={s(row.id),headers.value(static_cast<int>(evidence.column)+1),s(evidence.document_id),
            s(evidence.method),QString::number(evidence.confidence,'f',2),s(evidence.snippet)};
        for (int c=0;c<values.size();++c) check(workbook.currentWorksheet()->writeString(index,c+1,values[c],body));
        ++index;
    }
    workbook.setColumnWidth(1,5,35); workbook.setColumnWidth(6,100);
    workbook.selectSheet("İcra Dosyaları");
    QBuffer buffer; check(buffer.open(QIODevice::WriteOnly)); check(workbook.saveAs(&buffer));
    publish_workbook(buffer.data(),path,static_cast<int>(rows.size())+1);
}
} // namespace muz
