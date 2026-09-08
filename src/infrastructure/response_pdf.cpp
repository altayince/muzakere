#include "response_adapters.hpp"
#include "qt_paths.hpp"
#include "muz/domain/model.hpp"
#include <QFile>
#include <QTextDocument>
#include <QAbstractTextDocumentLayout>
#include <QPdfWriter>
#include <QPainter>
#include <QRegularExpression>
#include <QLocale>
#include <QMap>

static void init_response_templates() { Q_INIT_RESOURCE(response_templates); }
namespace muz {
QString response_html(const ResponseRow& row, const ResponseProfile& profile) {
    init_response_templates();
    auto read=[](const QString& path) {
        QFile file(path); if(!file.open(QIODevice::ReadOnly))throw Error(ErrorCode::file_io);
        return QString::fromUtf8(file.readAll());
    };
    auto source=read(":/responses/base.html");
    source.replace("{{paragraphs}}",read(row.available_cents?":/responses/var.html":":/responses/yok.html"));
    QString amount;
    if(row.available_cents) {
        const auto value=*row.available_cents, positive=value<0?-value:value;
        amount=(value<0?"-":"")+QLocale("tr_TR").toString(static_cast<qlonglong>(positive/100))+','+
            QString::number(positive%100).rightJustified(2,'0');
    }
    auto cell=[&](Column c){return QString::fromStdString(row.source.cells[c]).toHtmlEscaped();};
    const QMap<QString,QString> fields={
        {"office",cell(office)},{"case_number",cell(case_number)},{"creditor",cell(creditor)},
        {"debtor",cell(debtor)},{"debtor_id",cell(debtor_id)},{"service_date",cell(service_date)},
        {"recipient",QString::fromStdString(row.source.recipient).toHtmlEscaped()},
        {"lawyer",QString::fromStdString(profile.lawyer).toHtmlEscaped()},
        {"lawyer_address",QString::fromStdString(profile.address).toHtmlEscaped().replace('\n',"<br>")},
        {"amount",amount.toHtmlEscaped()},
        {"demo_banner",row.demo?QStringLiteral("<p style='color:#A00000;text-align:center'><b>TEST VERİSİ — TASLAK</b></p>"):QString{}}
    };
    QString result; qsizetype previous=0;
    auto matches=QRegularExpression(R"(\{\{([a-z_]+)\}\})").globalMatch(source);
    while(matches.hasNext()) {
        const auto match=matches.next(); result+=source.mid(previous,match.capturedStart()-previous);
        if(!fields.contains(match.captured(1)))throw Error(ErrorCode::invalid_input);
        result+=fields.value(match.captured(1)); previous=match.capturedEnd();
    }
    return result+source.mid(previous);
}

void write_response_pdf(const QString& html, const std::filesystem::path& output) {
    QFile file(qpath(output));
    if(!file.open(QIODevice::WriteOnly|QIODevice::NewOnly))throw Error(ErrorCode::file_io);
    {
        QPdfWriter writer(&file); writer.setPageSize(QPageSize(QPageSize::A4));
        writer.setPageMargins(QMarginsF(18,16,18,16)); writer.setResolution(144);
        writer.setTitle(QStringLiteral("Birinci haciz ihbarnamesine cevap")); writer.setCreator("Muzakere");
        QTextDocument document; document.setDefaultFont(QFont("Times New Roman",10));
        document.documentLayout()->setPaintDevice(&writer);
        document.setDocumentMargin(0);
        document.setPageSize(writer.pageLayout().paintRectPixels(writer.resolution()).size());
        document.setHtml(html); document.print(&writer);
    }
    if(!file.flush() || file.size()<100)throw Error(ErrorCode::file_io);
}
} // namespace muz
