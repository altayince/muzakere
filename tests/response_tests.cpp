#include "accounting_adapters.hpp"
#include "response_adapters.hpp"
#include "muz/infrastructure/local_workspace.hpp"
#include "qt_paths.hpp"
#include "response_panel.hpp"
#include "main_window.hpp"
#include <catch2/catch_test_macros.hpp>
#include <xlsxdocument.h>
#include <xlsxworksheet.h>
#include <xlsxcellformula.h>
#include <QTemporaryDir>
#include <QFile>
#include <QDir>
#include <QUuid>
#include <QApplication>
#include <QLineEdit>
#include <QTableWidget>
#include <QPushButton>
#include <QTabWidget>
#include <QElapsedTimer>
#include <QThread>

namespace {
std::vector<muz::AccountingRow> samples(int count=4) {
    std::vector<muz::AccountingRow> rows;
    for(int i=0;i<count;++i) {
        muz::AccountingRow row;row.id=QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
        row.document_id=row.id;row.batch_id="synthetic";row.envelope_id="envelope";row.sha256=std::string(64,'a');
        row.recipient="TEST MUHATAP A.Ş.";row.approved=true;
        row.cells={"07.09.2026","ANKARA 8. GENEL İCRA DAİRESİ","2026/42","1234,56",
            "TEST BORÇLU "+std::to_string(i+1),"0000000000"+std::to_string(i+1),"TEST ALACAKLI","TR000000000000000000000000","Evet","Test notu"};
        rows.push_back(row);
    }
    return rows;
}
QByteArray bytes(const QString& file) {QFile f(file);REQUIRE(f.open(QIODevice::ReadOnly));return f.readAll();}
void wait(muz::ResponsePanel& panel) {
    QElapsedTimer timer;timer.start();while(panel.busy() && timer.elapsed()<15000){QApplication::processEvents();QThread::msleep(10);}
    QApplication::processEvents();REQUIRE_FALSE(panel.busy());
}
}

TEST_CASE("Outgoing workbook has an empty rightmost accounting column; demo is distinct", "[integration][response]") {
    QTemporaryDir temp;REQUIRE(temp.isValid());const auto rows=samples();
    const auto output=temp.path()+"/out.xlsx";muz::write_accounting_xlsx(rows,muz::native_path(output),false);
    QXlsx::Document book(output);REQUIRE(book.load());REQUIRE(book.dimension().lastColumn()==14);
    REQUIRE(book.read(1,14).toString()=="Muhasebe");
    for(int r=2;r<=5;++r)REQUIRE(book.read(r,14).toString().isEmpty());
    const auto result=muz::read_accounting_return(bytes(output));
    REQUIRE(result.rows.size()==4);for(const auto& row:result.rows){REQUIRE(row.errors.empty());REQUIRE_FALSE(row.available_cents);}
    const auto demo=temp.path()+"/demo.xlsx";muz::write_accounting_xlsx(rows,muz::native_path(demo),true,true);
    const auto generated=muz::read_accounting_return(bytes(demo));
    REQUIRE(generated.rows[0].demo);REQUIRE(generated.rows[0].available_cents);REQUIRE_FALSE(generated.rows[1].available_cents);
}

TEST_CASE("Accounting return preserves zero and signed amounts and never treats bad input as YOK", "[integration][response]") {
    QTemporaryDir temp;REQUIRE(temp.isValid());const auto output=temp.path()+"/out.xlsx";
    muz::write_accounting_xlsx(samples(),muz::native_path(output),false);
    QXlsx::Document book(output);REQUIRE(book.load());
    REQUIRE(book.write(2,14,1234.56));REQUIRE(book.write(4,14,0));REQUIRE(book.write(5,14,-25.5));
    const auto returned=temp.path()+"/returned.xlsx";REQUIRE(book.saveAs(returned));
    auto data=muz::read_accounting_return(bytes(returned));
    REQUIRE(data.rows[0].available_cents==123456);REQUIRE_FALSE(data.rows[1].available_cents);
    REQUIRE(data.rows[2].available_cents==0);REQUIRE(data.rows[3].available_cents==-2550);
    REQUIRE(book.currentWorksheet()->writeFormula(2,14,QXlsx::CellFormula("1+1")));
    REQUIRE(book.write(3,14,"VAR"));REQUIRE(book.write(4,14,true));REQUIRE(book.write(5,14,1.234));
    const auto bad=temp.path()+"/bad.xlsx";REQUIRE(book.saveAs(bad));data=muz::read_accounting_return(bytes(bad));
    for(const auto& row:data.rows)REQUIRE_FALSE(row.errors.empty());
}

TEST_CASE("Returned rows can reorder but debtor identity edits are blocked", "[integration][response]") {
    QTemporaryDir temp;REQUIRE(temp.isValid());const auto output=temp.path()+"/out.xlsx";const auto rows=samples(2);
    muz::write_accounting_xlsx(rows,muz::native_path(output),false);
    QXlsx::Document book(output);REQUIRE(book.load());
    for(int c=1;c<=14;++c){const auto a=book.read(2,c),b=book.read(3,c);REQUIRE(book.write(2,c,b));REQUIRE(book.write(3,c,a));}
    REQUIRE(book.write(2,14,"1.234,56"));const auto sorted=temp.path()+"/sorted.xlsx";REQUIRE(book.saveAs(sorted));
    const auto data=muz::read_accounting_return(bytes(sorted));REQUIRE(data.rows[0].source.id==rows[1].id);
    REQUIRE(data.rows[0].available_cents==123456);REQUIRE(data.rows[0].errors.empty());
    REQUIRE(book.write(2,7,"99999999999"));const auto changed=temp.path()+"/changed.xlsx";REQUIRE(book.saveAs(changed));
    REQUIRE_FALSE(muz::read_accounting_return(bytes(changed)).rows[0].errors.empty());
}

TEST_CASE("Returned workbook persists and creates an individual VAR or YOK PDF for each selected row", "[integration][response][pdf]") {
    QTemporaryDir temp;REQUIRE(temp.isValid());const auto outgoing=temp.path()+"/out.xlsx";const auto rows=samples(2);
    muz::write_accounting_xlsx(rows,muz::native_path(outgoing),false);
    QXlsx::Document book(outgoing);REQUIRE(book.load());REQUIRE(book.write(2,14,5457.34));
    const auto returned=temp.path()+"/returned.xlsx";REQUIRE(book.saveAs(returned));
    const auto root=temp.path()+"/workspace";std::string id;
    {
        muz::LocalWorkspace workspace(muz::native_path(root));const auto data=workspace.import_accounting_return(muz::native_path(returned));id=data.id;
        REQUIRE(data.rows.size()==2);REQUIRE(data.rows[0].errors.empty());REQUIRE(data.rows[1].errors.empty());
        const muz::ResponseProfile profile{"Av. TEST VEKİLİ","TEST ADRESİ, İSTANBUL"};
        const auto folder=workspace.generate_responses(id,{data.rows[0].id,data.rows[1].id},muz::native_path(temp.path()+"/pdfs"),profile);
        const auto files=QDir(muz::qpath(folder)).entryList({"*.pdf"},QDir::Files);REQUIRE(files.size()==2);
        bool var=false,yok=false;
        for(const auto& file:files) {
            const auto text=muz::extract_pdf(bytes(muz::qpath(folder)+'/'+file));REQUIRE(text.error.empty());
            REQUIRE(text.text.contains(QStringLiteral("TEST MUHATAP")));REQUIRE(text.text.contains("2026/42"));
            if(file.endsWith("_VAR.pdf")){var=true;REQUIRE(text.text.contains("5.457,34"));REQUIRE(text.text.contains(QStringLiteral("TEST BORÇLU 1")));}
            else {yok=true;REQUIRE(text.text.simplified().contains(QStringLiteral("tespit edilememiştir")));REQUIRE(text.text.contains(QStringLiteral("TEST BORÇLU 2")));}
        }
        REQUIRE(var);REQUIRE(yok);REQUIRE(QFile::exists(muz::qpath(folder)+"/manifest.json"));
        REQUIRE_FALSE(workspace.response_rows(id)[0].output_pdf.empty());
        REQUIRE(workspace.response_profile().lawyer==profile.lawyer);
        REQUIRE_THROWS_AS(workspace.generate_responses(id,{data.rows[0].id,data.rows[0].id},muz::native_path(temp.path()+"/duplicates"),profile),muz::Error);
        QFile preserved(root+'/'+QString::fromStdString(data.managed_path));REQUIRE(preserved.open(QIODevice::WriteOnly));preserved.write("tampered");preserved.close();
        REQUIRE_THROWS_AS(workspace.generate_responses(id,{data.rows[0].id},muz::native_path(temp.path()+"/bad"),profile),muz::Error);
    }
    muz::LocalWorkspace reopened(muz::native_path(root));REQUIRE(reopened.accounting_returns().size()==1);
    REQUIRE(reopened.response_rows(id).size()==2);reopened.reset_database_for_testing();
    REQUIRE(reopened.accounting_returns().empty());REQUIRE(reopened.response_rows(id).empty());
}

TEST_CASE("Second workflow tab imports, previews and generates PDFs with native UI actions", "[integration][desktop][response]") {
    QTemporaryDir temp;REQUIRE(temp.isValid());const auto output=temp.path()+"/return.xlsx";
    muz::write_accounting_xlsx(samples(2),muz::native_path(output),true,true);
    muz::LocalWorkspace workspace(muz::native_path(temp.path()+"/workspace"));
    {muz::MainWindow window(workspace);auto* tabs=window.findChild<QTabWidget*>("workflowTabs");REQUIRE(tabs!=nullptr);REQUIRE(tabs->count()==2);}
    bool active=false;muz::ResponsePanel panel(workspace,[&](bool busy){active=busy;});panel.show();panel.importWorkbook(muz::native_path(output));
    REQUIRE(active);wait(panel);REQUIRE_FALSE(active);
    auto* table=panel.findChild<QTableWidget*>("responseTable");REQUIRE(table!=nullptr);REQUIRE(table->rowCount()==2);
    panel.findChild<QLineEdit*>("responseLawyer")->setText(QStringLiteral("Av. TEST"));
    panel.findChild<QLineEdit*>("responseAddress")->setText(QStringLiteral("TEST ADRES"));
    panel.findChild<QPushButton*>("selectValidResponses")->click();panel.generatePdfs(muz::native_path(temp.path()+"/out"));wait(panel);
    REQUIRE_FALSE(workspace.response_rows(workspace.accounting_returns()[0].id)[0].output_pdf.empty());
}
