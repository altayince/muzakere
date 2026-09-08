#include "response_panel.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTableWidget>
#include <QHeaderView>
#include <QPushButton>
#include <QLabel>
#include <QLineEdit>
#include <QTextBrowser>
#include <QSplitter>
#include <QFileDialog>
#include <QDesktopServices>
#include <QUrl>
#include <QtConcurrentRun>

namespace muz {
namespace {
QString s(const std::string& value){return QString::fromStdString(value);}
std::filesystem::path path(const QString& value){
    const auto bytes=value.toUtf8();return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(bytes.constData()),static_cast<std::size_t>(bytes.size())));
}
void configure(QTableWidget* table,const QStringList& headers) {
    table->setColumnCount(headers.size());table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->horizontalHeader()->setStretchLastSection(true);table->verticalHeader()->hide();
}
}
ResponsePanel::ResponsePanel(Workspace& workspace,std::function<void(bool)> activity,QWidget* parent)
    :QWidget(parent),workspace_(workspace),activity_(std::move(activity)) {
    auto* layout=new QVBoxLayout(this);
    layout->addWidget(new QLabel(QStringLiteral("Muhasebe dönüşü → VAR/YOK cevabı → dosya başına PDF"),this));
    auto* info=new QLabel(QStringLiteral("HAMDATA ve MUHASEBE sayfalarını içeren dönüş Excel’ini yükleyin. Tutar T.C./vergi numarasıyla borçlunun tüm dosyalarına bağlanır. Sayı VAR (0 dahil), boş tutar YOK; her dosyaya ayrı PDF."),this);
    info->setWordWrap(true);layout->addWidget(info);
    auto* toolbar=new QHBoxLayout;
    auto button=[&](const QString& label,const char* name) {
        auto* item=new QPushButton(label,this);item->setObjectName(name);toolbar->addWidget(item);buttons_.push_back(item);return item;
    };
    auto* upload=button(QStringLiteral("Muhasebe Excel’i yükle…"),"importReturnWorkbook");
    auto* select=button(QStringLiteral("Uygun satırları seç"),"selectValidResponses");
    auto* generate=button(QStringLiteral("Seçili PDF’leri oluştur…"),"generateResponsePdfs");
    auto* open=button(QStringLiteral("Çıktı klasörünü aç"),"openResponseDirectory");
    layout->addLayout(toolbar);
    auto* form=new QHBoxLayout;
    lawyer_=new QLineEdit(this);lawyer_->setObjectName("responseLawyer");lawyer_->setPlaceholderText(QStringLiteral("Vekil: Av. Ad Soyad"));
    address_=new QLineEdit(this);address_->setObjectName("responseAddress");address_->setPlaceholderText(QStringLiteral("Vekil adresi"));
    const auto defaults=workspace_.response_profile();lawyer_->setText(s(defaults.lawyer));address_->setText(s(defaults.address));
    form->addWidget(lawyer_,1);form->addWidget(address_,3);layout->addLayout(form);
    auto* vertical=new QSplitter(Qt::Vertical,this);
    batches_=new QTableWidget(vertical);batches_->setObjectName("returnBatches");
    configure(batches_,{QStringLiteral("Dönüş ID"),QStringLiteral("Excel dosyası"),QStringLiteral("Alınma tarihi")});
    batches_->setSelectionMode(QAbstractItemView::SingleSelection);batches_->setColumnWidth(0,280);batches_->setColumnWidth(1,350);
    auto* detail=new QSplitter(Qt::Horizontal,vertical);
    table_=new QTableWidget(detail);table_->setObjectName("responseTable");
    configure(table_,{QStringLiteral("Durum"),QStringLiteral("İcra Dairesi"),QStringLiteral("Esas"),QStringLiteral("Borçlu"),
        QStringLiteral("TCKN/VKN"),QStringLiteral("Muhatap"),QStringLiteral("Muhasebe"),QStringLiteral("VAR/YOK"),QStringLiteral("PDF"),QStringLiteral("Uyarılar")});
    table_->setSelectionMode(QAbstractItemView::ExtendedSelection);table_->setColumnWidth(1,200);table_->setColumnWidth(3,180);
    preview_=new QTextBrowser(detail);preview_->setObjectName("responsePreview");
    preview_->setOpenExternalLinks(false);preview_->setOpenLinks(false);
    detail->setSizes({650,450});vertical->setSizes({150,550});layout->addWidget(vertical,1);
    status_=new QLabel(QStringLiteral("Dönen Excel’i yükleyin. HAMDATA dosya satırlarını, MUHASEBE tekil borçluları içerir."),this);
    status_->setWordWrap(true);status_->setTextFormat(Qt::PlainText);layout->addWidget(status_);
    connect(upload,&QPushButton::clicked,this,[this]{const auto file=QFileDialog::getOpenFileName(this,QStringLiteral("Muhasebeden dönen Excel"),{},"Excel (*.xlsx)");if(!file.isEmpty())importWorkbook(path(file));});
    connect(select,&QPushButton::clicked,this,[this] {
        table_->clearSelection();
        for(std::size_t i=0;i<rows_.size();++i)if(rows_[i].errors.empty())table_->selectionModel()->select(table_->model()->index(static_cast<int>(i),0),QItemSelectionModel::Select|QItemSelectionModel::Rows);
    });
    connect(generate,&QPushButton::clicked,this,[this]{const auto folder=QFileDialog::getExistingDirectory(this,QStringLiteral("PDF çıktı klasörü"));if(!folder.isEmpty())generatePdfs(path(folder));});
    connect(open,&QPushButton::clicked,this,[this]{if(!output_.empty())QDesktopServices::openUrl(QUrl::fromLocalFile(s(output_)));});
    connect(lawyer_,&QLineEdit::textChanged,this,[this]{showPreview();});connect(address_,&QLineEdit::textChanged,this,[this]{showPreview();});
    connect(table_,&QTableWidget::itemSelectionChanged,this,[this]{showPreview();});
    connect(batches_,&QTableWidget::itemSelectionChanged,this,[this]{
        const int row=batches_->currentRow();selected_=row>=0?batches_->item(row,0)->text().toStdString():std::string{};showRows();
    });
    connect(&watcher_,&QFutureWatcher<Outcome>::finished,this,[this] {
        const auto result=watcher_.result();setBusy(false);
        if(!result.error.empty()) {
            status_->setText(QStringLiteral("İşlem tamamlanamadı. HAMDATA/MUHASEBE sayfalarını ve T.C./vergi numaralarını kontrol edin. PDF için geçerli satırları seçin ve vekil/adres bilgilerini doldurun. Hata: ")+s(result.error));
            return;
        }
        reload(result.return_id);
        if(!result.output.empty()){output_=result.output;status_->setText(QStringLiteral("PDF’ler oluşturuldu: ")+s(output_));}
    });
    reload();
}
ResponsePanel::~ResponsePanel(){watcher_.waitForFinished();}
ResponseProfile ResponsePanel::profile() const{return {lawyer_->text().trimmed().toStdString(),address_->text().trimmed().toStdString()};}
void ResponsePanel::setBusy(bool value) {
    for(auto* button:buttons_)button->setEnabled(!value);
    batches_->setEnabled(!value);table_->setEnabled(!value);lawyer_->setEnabled(!value);address_->setEnabled(!value);
    if(activity_)activity_(value);
}
void ResponsePanel::reload(const std::string& selected) {
    try {
        const auto data=workspace_.accounting_returns();const QSignalBlocker blocker(batches_);batches_->setRowCount(0);
        int selection=0;
        for(const auto& item:data) {
            const int r=batches_->rowCount();batches_->insertRow(r);if(item.id==selected)selection=r;
            const QStringList values={s(item.id),s(item.source_name),s(item.created_at)};
            for(int c=0;c<values.size();++c)batches_->setItem(r,c,new QTableWidgetItem(values[c]));
        }
        if(!data.empty()){batches_->selectRow(selection);selected_=data[static_cast<std::size_t>(selection)].id;}
        else {selected_.clear();output_.clear();}
        showRows();
    }catch(const Error& error){status_->setText(s(error.what()));}
}
void ResponsePanel::showRows() {
    try {
        const QSignalBlocker blocker(table_);table_->setRowCount(0);preview_->clear();
        rows_=selected_.empty()?std::vector<ResponseRow>{}:workspace_.response_rows(selected_);
        int valid=0,var=0;
        for(const auto& item:rows_) {
            const int r=table_->rowCount();table_->insertRow(r);QStringList errors;for(const auto& e:item.errors)errors.append(s(e));
            for(const auto& warning:item.source.warnings)errors.append(s(warning));
            const auto state=item.errors.empty()?(item.demo?QStringLiteral("TEST — Hazır"):
                item.source.warnings.empty()?QStringLiteral("Hazır"):QStringLiteral("Kontrol edin")):QStringLiteral("İlerlenemiyor");
            const QStringList values={state,s(item.source.cells[office]),s(item.source.cells[case_number]),s(item.source.cells[debtor]),
                s(item.source.cells[debtor_id]),s(item.source.recipient),s(item.accounting_input),
                item.errors.empty()?(item.available_cents?"VAR":"YOK"):"—",s(item.output_pdf),errors.join("; ")};
            const auto color=!item.errors.empty()?"#F8D7DA":item.source.warnings.empty()?"#D4EDDA":"#FFF3CD";
            for(int c=0;c<values.size();++c){auto* cell=new QTableWidgetItem(values[c]);cell->setBackground(QColor(color));cell->setForeground(Qt::black);table_->setItem(r,c,cell);}
            if(item.errors.empty()){++valid;if(item.available_cents)++var;}
        }
        status_->setText(QStringLiteral("%1 satır · %2 VAR · %3 YOK · %4 inceleme gerekli").arg(static_cast<int>(rows_.size())).arg(var).arg(valid-var).arg(static_cast<int>(rows_.size())-valid));
    }catch(const Error& error){status_->setText(s(error.what()));}
}
void ResponsePanel::showPreview() {
    const int row=table_->currentRow();if(row<0 || row>=static_cast<int>(rows_.size())){preview_->clear();return;}
    try {preview_->setHtml(s(workspace_.preview_response(rows_[static_cast<std::size_t>(row)],profile())));}
    catch(const Error&){preview_->setPlainText(QStringLiteral("Bu satırın uyarıları çözülmeden PDF oluşturulamaz."));}
}
void ResponsePanel::importWorkbook(const std::filesystem::path& input) {
    if(busy())return;
    setBusy(true);status_->setText(QStringLiteral("Excel okunuyor, dosya/borçlu kimlikleri doğrulanıyor…"));
    watcher_.setFuture(QtConcurrent::run([this,input] {
        try{return Outcome{workspace_.import_accounting_return(input).id,{},{}};}
        catch(const Error& error){return Outcome{{},error.what(),{}};}catch(...){return Outcome{{},"unexpected_error",{}};}
    }));
}
void ResponsePanel::generatePdfs(const std::filesystem::path& output) {
    if(busy() || selected_.empty())return;
    std::vector<std::string> ids;for(const auto& index:table_->selectionModel()->selectedRows())ids.push_back(rows_.at(static_cast<std::size_t>(index.row())).id);
    if(ids.empty()){status_->setText(QStringLiteral("Önce PDF oluşturulacak satırları seçin."));return;}
    const auto selected=selected_;const auto details=profile();setBusy(true);status_->setText(QStringLiteral("PDF’ler oluşturuluyor…"));
    watcher_.setFuture(QtConcurrent::run([this,selected,ids,output,details] {
        try {const auto folder=workspace_.generate_responses(selected,ids,output,details).generic_u8string();return Outcome{selected,{},std::string(reinterpret_cast<const char*>(folder.data()),folder.size())};}
        catch(const Error& error){return Outcome{selected,error.what(),{}};}catch(...){return Outcome{selected,"unexpected_error",{}};}
    }));
}
} // namespace muz
