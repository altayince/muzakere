#include "accounting_adapters.hpp"
#include "muz/infrastructure/local_workspace.hpp"
#include "qt_paths.hpp"
#include "schema.hpp"
#include <catch2/catch_test_macros.hpp>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QPdfWriter>
#include <QPainter>
#include <QBuffer>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QVariant>
#include <xlsxdocument.h>
#include <miniz.h>
#include "main_window.hpp"
#include <QTableWidget>
#include <QPushButton>
#include <QElapsedTimer>
#include <QThread>
#include <QApplication>

namespace {
QString content() {
    return QStringLiteral("T.C.\nANKARA\n8. GENEL İCRA DAİRESİ\n2026/42 ESAS 28/08/2026\n"
        "BİRİNCİ HACİZ İHBARNAMESİ\n"
        "1. Üçüncü şahsın adı, soyadı ve adresi : ÖRNEK ŞİRKET\n"
        "2. Alacaklının ve varsa vekilinin adı, soyadı ve adresi : ÖRNEK ALACAKLI A.Ş.\nvekili Av. Örnek Vekil\n"
        "3. Borçlunun ve varsa vekilinin adı, soyadı ve adresi : ÖRNEK BORÇLU, 00000000000 TC Nolu,\n"
        "4. Haczin neye ilişkin olduğu, hangi miktar için yapıldığı : Hak ve alacaklar\n"
        "5. Alacak tutarı ile faiz ve giderler : 1.234,56 TL\n"
        "İban No : TR000000000000000000000000\n");
}
QString envelope() {
    return QStringLiteral("8. Genel İcra Dairesi\nDosya No: 2026/42 İcra\nTEBLİĞ MAZBATASI\nANKARA\nT.C.\nBU ZARFTA Haciz Yazısı VARDIR.");
}
QByteArray pdf(const QString& text) {
    QBuffer buffer; REQUIRE(buffer.open(QIODevice::WriteOnly));
    { QPdfWriter writer(&buffer); writer.setResolution(96);
      QPainter painter(&writer); painter.setFont(QFont("Arial",9));
      painter.drawText(QRect(0,0,700,1000),Qt::TextWordWrap,text); }
    return buffer.data();
}
void write_file(const QString& path, const QByteArray& data) {
    QFile f(path); REQUIRE(f.open(QIODevice::WriteOnly)); REQUIRE(f.write(data)==data.size());
}
QByteArray zip(const std::vector<std::pair<std::string,QByteArray>>& entries) {
    mz_zip_archive writer{}; REQUIRE(mz_zip_writer_init_heap(&writer,0,0));
    for (const auto& [name,data]:entries) REQUIRE(mz_zip_writer_add_mem(&writer,name.c_str(),data.constData(),
        static_cast<size_t>(data.size()),MZ_DEFAULT_COMPRESSION));
    void* bytes=nullptr; size_t size=0; REQUIRE(mz_zip_writer_finalize_heap_archive(&writer,&bytes,&size));
    QByteArray result(static_cast<const char*>(bytes),static_cast<qsizetype>(size));
    mz_free(bytes); mz_zip_writer_end(&writer); return result;
}
muz::ParsedDocument parsed(const QString& text, const char* id, const char* source) {
    muz::IncomingDocument d; d.id=id;d.batch_id="batch";d.source_path=source;d.file.sha256=std::string(64,'a');
    return muz::parse_document(d,{text,{}});
}
}

TEST_CASE("Turkish money parser preserves cents and rejects ambiguous formats", "[unit][accounting]") {
    REQUIRE(muz::parse_money("1.234,56")==123456);
    REQUIRE(muz::parse_money("0,01")==1);
    REQUIRE(muz::format_money(123456)=="1234,56");
    for(const auto* invalid:{"1,234.56","1.23,45","12","-1,00","1,234","1.234,5","1.23.456,78","1..234,56"})
        REQUIRE_FALSE(muz::parse_money(invalid));
}

TEST_CASE("Field extraction uses content labels without inventing a service date", "[unit][extraction]") {
    const auto doc=parsed(content(),"doc","a/content.pdf");
    INFO(muz::encode_row(doc.row).toStdString());
    REQUIRE(doc.row.cells[muz::case_number]=="2026/42");
    REQUIRE(doc.row.cells[muz::office]=="ANKARA 8. GENEL İCRA DAİRESİ");
    REQUIRE(doc.row.cells[muz::debtor]=="ÖRNEK BORÇLU");
    REQUIRE(doc.row.cells[muz::debtor_id]=="00000000000");
    REQUIRE(doc.row.cells[muz::creditor]=="ÖRNEK ALACAKLI A.Ş.");
    REQUIRE(doc.row.cells[muz::amount]=="1.234,56");
    REQUIRE(doc.row.cells[muz::first_notice]=="Evet");
    REQUIRE(doc.row.cells[muz::service_date].empty());
    REQUIRE_FALSE(doc.row.approved);
    REQUIRE_FALSE(doc.row.evidence.empty());
}

TEST_CASE("General letter classification ignores footer references to article 89", "[unit][extraction]") {
    auto text=content(); text.replace("BİRİNCİ HACİZ İHBARNAMESİ","ADRES ARAŞTIRMA YAZISI");
    text += "\nİİK 89/1 haciz ihbarnamesi genel mevzuat açıklaması";
    REQUIRE(parsed(text,"doc","a/general.pdf").row.cells[muz::first_notice]=="Hayır");
    REQUIRE(parsed("", "scan", "a/scan.pdf").row.cells[muz::first_notice]=="Belirsiz");
    for(const auto& wording:{QStringLiteral("Konulan haczin kaldırılmasına karar verilmiştir."),
                             QStringLiteral("Konulmuş haczin bu dosyamıza şamil olmak üzere fekkine karar verilmiştir.")}) {
        auto release=content();release.replace("BİRİNCİ HACİZ İHBARNAMESİ","DAĞITIM YERLERİNE");release+=wording;
        REQUIRE(parsed(release,"release","a/general.pdf").row.cells[muz::first_notice]=="Hayır");
    }
}

TEST_CASE("Envelope matching needs identifiers and retains orphan envelopes", "[unit][matching]") {
    auto doc=parsed(content(),"doc","a/content.pdf");
    auto env=parsed(envelope(),"env","a/envelope.pdf");
    auto matched=muz::match_accounting({doc,env});
    INFO(muz::encode_row(doc.row).toStdString()); INFO(muz::encode_row(env.row).toStdString());
    REQUIRE(matched.size()==1); REQUIRE(matched[0].envelope_id=="env");
    auto wrong=envelope(); wrong.replace("2026/42","2026/99");
    auto conflict=muz::match_accounting({doc,parsed(wrong,"env","a/envelope.pdf")});
    REQUIRE(conflict.size()==2); REQUIRE(conflict[0].envelope_id.empty());
    auto ambiguous=muz::match_accounting({doc,env,parsed(envelope(),"env2","a/other.pdf")});
    REQUIRE(ambiguous.size()==3); REQUIRE(ambiguous[0].envelope_id.empty());
}

TEST_CASE("Native PDF worker extracts text and rejects invalid documents", "[integration][pdf]") {
    const auto result=muz::extract_pdf(pdf(content()));
    INFO(result.error);
    REQUIRE(result.error.empty()); REQUIRE(result.text.contains("2026/42"));
    REQUIRE(result.text.contains(QStringLiteral("ÖRNEK BORÇLU")));
    REQUIRE_FALSE(muz::extract_pdf("not a PDF").error.empty());
}

TEST_CASE("Same case in the same folder does not override a recipient conflict", "[unit][matching]") {
    auto doc=parsed(content(),"doc","a/content.pdf");
    auto env=parsed(envelope(),"env","a/envelope.pdf");
    doc.row.recipient="ÖRNEK GETİR AŞ";env.row.recipient="ÖRNEK YEMEK AŞ";
    const auto rows=muz::match_accounting({doc,env});
    REQUIRE(rows.size()==2);REQUIRE(rows[0].envelope_id.empty());
    REQUIRE(rows[0].pair_conflict);REQUIRE_FALSE(muz::can_approve(rows[0]));
}

TEST_CASE("ZIP paths cannot escape the destination or collide", "[integration][zip]") {
    QTemporaryDir temp; REQUIRE(temp.isValid());
    for(const auto* name:{"../escape.txt","a/../../escape.txt","NUL.txt"})
        REQUIRE_THROWS_AS(muz::extract_zip(zip({{name,"data"}}),muz::native_path(temp.path())),muz::Error);
    REQUIRE_THROWS_AS(muz::extract_zip(zip({{"a.txt","one"},{"A.txt","two"}}),muz::native_path(temp.path())),muz::Error);
}

TEST_CASE("ZIP to reviewed Excel round trip preserves IDs, text cells and approval", "[integration][accounting]") {
    QTemporaryDir temp; REQUIRE(temp.isValid());
    const auto archive=temp.path()+"/input.zip";
    write_file(archive,zip({{"a/content.pdf",pdf(content())},{"a/envelope.pdf",pdf(envelope())}}));
    const auto root=temp.path()+"/workspace";
    std::string id, row_id;
    {
        muz::LocalWorkspace workspace(muz::native_path(root));
        const auto result=workspace.import_archive(muz::native_path(archive)); id=result.batch.id;
        REQUIRE(result.batch.imported_count==2);
        REQUIRE(result.documents[0].source_path.find("input.zip!/")!=std::string::npos);
        auto rows=workspace.prepare_accounting(id);
        REQUIRE(rows.size()==1); row_id=rows[0].id;
        REQUIRE(rows[0].cells[muz::debtor]=="ÖRNEK BORÇLU");
        REQUIRE_THROWS_AS(workspace.export_accounting(id,muz::native_path(temp.path()+"/unapproved.xlsx"),false),muz::Error);
        workspace.export_accounting(id,muz::native_path(temp.path()+"/draft.xlsx"),true);
        QXlsx::Document draft(temp.path()+"/draft.xlsx"); REQUIRE(draft.load());
        REQUIRE(draft.read(2,11).toString().contains(QStringLiteral("İNCELEME TASLAĞI")));
        rows[0].cells[muz::service_date]="31.08.2026";
        rows[0].cells[muz::notes]="=HYPERLINK(\"https://example.invalid\")";
        rows[0].approved=true;workspace.review_accounting(id,rows);
        workspace.export_accounting(id,muz::native_path(temp.path()+"/approved.xlsx"),false);
        REQUIRE_THROWS_AS(workspace.export_accounting(id,muz::native_path(temp.path()+"/approved.xlsx"),false),muz::Error);
        QXlsx::Document book(temp.path()+"/approved.xlsx"); REQUIRE(book.load());
        REQUIRE(book.read(1,7).toString()=="Borçlu TCKN/VKN");
        REQUIRE(book.read(2,7).toString()=="00000000000");
        REQUIRE(book.read(2,5).toDouble()==1234.56);
        REQUIRE(book.read(2,11).toString().startsWith("=HYPERLINK"));
        REQUIRE(book.selectSheet("Kaynaklar")); REQUIRE(book.read(2,2).toString().toStdString()==row_id);
        REQUIRE(workspace.prepare_accounting(id)[0].approved);
        const auto original=workspace.documents(id).front().file.managed_path;
        write_file(root+'/'+QString::fromStdString(original),"tampered");
        REQUIRE_THROWS_AS(workspace.export_accounting(id,muz::native_path(temp.path()+"/tampered.xlsx"),false),muz::Error);
    }
    muz::LocalWorkspace reopened(muz::native_path(root));
    REQUIRE(reopened.accounting_rows(id)[0].approved);
    REQUIRE(reopened.accounting_rows(id)[0].id==row_id);
}

TEST_CASE("Desktop ZIP import exposes editable rows and persists explicit approval", "[integration][desktop]") {
    QTemporaryDir temp;REQUIRE(temp.isValid());
    const auto archive=temp.path()+"/input.zip";
    write_file(archive,zip({{"a/content.pdf",pdf(content())},{"a/envelope.pdf",pdf(envelope())}}));
    muz::LocalWorkspace workspace(muz::native_path(temp.path()+"/workspace"));
    muz::MainWindow window(workspace);window.show();window.importArchive(muz::native_path(archive));
    QElapsedTimer timer;timer.start();
    while(window.busy() && timer.elapsed()<10000){QApplication::processEvents();QThread::msleep(10);}
    QApplication::processEvents();REQUIRE_FALSE(window.busy());REQUIRE(window.batchCount()==1);
    QTableWidget* table=nullptr;
    for(auto* candidate:window.findChildren<QTableWidget*>())if(candidate->columnCount()==12)table=candidate;
    REQUIRE(table!=nullptr);REQUIRE(table->rowCount()==1);
    table->selectRow(0);table->item(0,1)->setText("31.08.2026");
    QPushButton* approve=nullptr;
    for(auto* candidate:window.findChildren<QPushButton*>())if(candidate->text()==QStringLiteral("Seçilenleri onayla"))approve=candidate;
    REQUIRE(approve!=nullptr);approve->click();QApplication::processEvents();
    const auto rows=workspace.accounting_rows(workspace.batches()[0].id);
    REQUIRE(rows.size()==1);REQUIRE(rows[0].approved);REQUIRE(rows[0].cells[muz::service_date]=="31.08.2026");
}

TEST_CASE("Schema v1 upgrades without losing preexisting batches", "[integration][migration]") {
    QTemporaryDir temp; REQUIRE(temp.isValid());
    REQUIRE(QDir().mkpath(temp.path()+"/workspace/database"));
    {
        auto db=QSqlDatabase::addDatabase("QSQLITE","migration-fixture");
        db.setDatabaseName(temp.path()+"/workspace/database/muzakere.sqlite3");REQUIRE(db.open());
        QSqlQuery query(db);
        for(const auto& statement:QString::fromUtf8(muz::schema_v1).split("-- statement",Qt::SkipEmptyParts)) REQUIRE(query.exec(statement));
        REQUIRE(query.exec("INSERT INTO processing_batches VALUES('existing','2026-01-01T00:00:00Z','imported',0,0,0)"));
    }
    QSqlDatabase::removeDatabase("migration-fixture");
    muz::LocalWorkspace workspace(muz::native_path(temp.path()+"/workspace"));
    REQUIRE(workspace.batches().size()==1); REQUIRE(workspace.batches()[0].id=="existing");
    REQUIRE(workspace.accounting_rows("existing").empty());
}
