#include "main_window.hpp"

#include <QCloseEvent>
#include <QFileDialog>
#include <QHeaderView>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QtConcurrentRun>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QHBoxLayout>
#include <QMessageBox>
#include <algorithm>

namespace muz {
namespace {
void configure(QTableWidget* table, const QStringList& headers) {
    table->setColumnCount(headers.size());
    table->setHorizontalHeaderLabels(headers);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setAlternatingRowColors(true);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->hide();
}
void row(QTableWidget* table, const QStringList& values) {
    const auto index = table->rowCount();
    table->insertRow(index);
    for (int column = 0; column < values.size(); ++column)
        table->setItem(index, column, new QTableWidgetItem(values[column]));
}
QString s(const std::string& value) { return QString::fromStdString(value); }
std::filesystem::path native(const QString& value) {
    const auto bytes = value.toUtf8();
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(bytes.constData()),static_cast<std::size_t>(bytes.size())));
}
QString errorText(const std::string& code) {
    if (code == "invalid_input") return QStringLiteral("Boş klasör, çalışma alanıyla çakışan klasör veya desteklenmeyen dosya bağlantısı.");
    if (code == "integrity_check_failed") return QStringLiteral("Dosya bütünlüğü doğrulanamadı. Kaynak ve arşiv dosyasını inceleyin.");
    if (code == "file_io") return QStringLiteral("Dosya okunamadı veya güvenli kopyası yazılamadı. Erişim ve disk alanını kontrol edin.");
    return QStringLiteral("İşlem tamamlanamadı. Hata kodu: ") + s(code);
}
} // namespace

MainWindow::MainWindow(Workspace& workspace) : workspace_(workspace) {
    setWindowTitle(QStringLiteral("Müzakere — Yerel KEP Dosyaları"));
    resize(1180, 760);
    auto* central = new QWidget(this);
    auto* layout = new QVBoxLayout(central);
    layout->addWidget(new QLabel(QStringLiteral("Belge içe aktarma ve batch takibi"), central));
    layout->addWidget(new QLabel(QStringLiteral("Belgeler yerel arşivde korunur. Otomatik çıkarımlar ve zarf eşleştirmeleri kullanıcı incelemesi gerektirir."), central));
    import_ = new QPushButton(QStringLiteral("Klasörden &belge al…  (Ctrl+I)"), central);
    import_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    layout->addWidget(import_);
    auto* zip_button = new QPushButton(QStringLiteral("ZIP arşivi al ve Excel satırlarını hazırla…"),central);
    layout->addWidget(zip_button); actions_.push_back(zip_button);
    auto* reset_button = new QPushButton(QStringLiteral("TEST — Veritabanını sıfırla"), central);
    reset_button->setObjectName("resetDatabaseForTesting");
    reset_button->setToolTip(QStringLiteral("Tüm işlem gruplarını, satırları, onayları ve işlem geçmişini siler."));
    layout->addWidget(reset_button); actions_.push_back(reset_button);
    connect(reset_button, &QPushButton::clicked, this, &MainWindow::resetDatabaseForTesting);
    connect(zip_button,&QPushButton::clicked,this,[this] {
        const auto file=QFileDialog::getOpenFileName(this,QStringLiteral("Tebligat arşivi seçin"),{},"ZIP (*.zip)");
        if (!file.isEmpty()) importArchive(native(file));
    });
    progress_ = new QProgressBar(central);
    progress_->setRange(0, 0);
    progress_->hide();
    layout->addWidget(progress_);
    auto* splitter = new QSplitter(Qt::Vertical, central);
    batches_ = new QTableWidget(splitter);
    configure(batches_, {QStringLiteral("Batch ID"), QStringLiteral("Oluşturulma (UTC)"), QStringLiteral("Durum"),
        QStringLiteral("Belge"), QStringLiteral("Aynı içerik"), QStringLiteral("Sorun")});
    batches_->setColumnWidth(0, 300);
    batches_->setColumnWidth(1, 210);
    auto* tabs = new QTabWidget(splitter);
    documents_ = new QTableWidget(tabs);
    configure(documents_, {QStringLiteral("Dosya adı"), QStringLiteral("Tür"), QStringLiteral("Boyut"), QStringLiteral("Tekrar"), QStringLiteral("SHA-256")});
    documents_->setColumnWidth(0, 280);
    documents_->setColumnWidth(1, 180);
    issues_ = new QTableWidget(tabs);
    configure(issues_, {QStringLiteral("Kaynak dosya"), QStringLiteral("İnceleme nedeni")});
    issues_->setColumnWidth(0, 420);
    tabs->addTab(documents_, QStringLiteral("Belgeler"));
    tabs->addTab(issues_, QStringLiteral("İnceleme gerekenler"));
    auto* review = new QWidget(tabs);
    auto* review_layout = new QVBoxLayout(review);
    auto* toolbar = new QHBoxLayout;
    auto button = [&](const QString& label) {
        auto* result = new QPushButton(label,review); toolbar->addWidget(result); actions_.push_back(result); return result;
    };
    auto* prepare=button(QStringLiteral("Satırları hazırla"));
    auto* select_all=button(QStringLiteral("Tümünü seç"));
    auto* save=button(QStringLiteral("Seçilenleri kaydet"));
    auto* approve=button(QStringLiteral("Seçilenleri onayla"));
    auto* draft=button(QStringLiteral("İnceleme Excel’i"));
    auto* export_button=button(QStringLiteral("Onaylı Excel"));
    review_layout->addLayout(toolbar);
    auto* date_layout=new QHBoxLayout;
    date_=new QLineEdit(review); date_->setPlaceholderText(QStringLiteral("Tebliğ tarihini değiştir: gg.aa.yyyy"));
    auto* apply_date=new QPushButton(QStringLiteral("Tarihi seçilenlere uygula"),review); actions_.push_back(apply_date);
    date_layout->addWidget(date_); date_layout->addWidget(apply_date); review_layout->addLayout(date_layout);
    review_layout->addWidget(new QLabel(QStringLiteral("Tebliğ tarihi şimdilik satırların hazırlandığı günün tarihiyle doldurulur; düzenleyebilirsiniz."),review));
    review_layout->addWidget(new QLabel(QStringLiteral("Alanları düzenleyin, kaynak metni ve uyarıları inceleyin. Onay, seçili satırların uyarılarıyla birlikte kabulüdür."),review));
    review_layout->addWidget(new QLabel(QStringLiteral("Yeşil: Sorunsuz   •   Sarı: İnceleme gerekli   •   Kırmızı: İlerlenemiyor / onay engeli var"),review));
    auto* review_splitter=new QSplitter(Qt::Vertical,review);
    accounting_=new QTableWidget(review_splitter);
    configure(accounting_,{QStringLiteral("Durum"),QStringLiteral("Tebliğ Tarihi"),QStringLiteral("İcra Dairesi"),
        QStringLiteral("Esas Numarası"),QStringLiteral("Borç Miktarı (TL)"),QStringLiteral("Borçlu"),QStringLiteral("Borçlu TCKN/VKN"),
        QStringLiteral("Alacaklı"),QStringLiteral("İcra Dairesi İBAN"),QStringLiteral("89/1?"),QStringLiteral("Açıklama"),QStringLiteral("Muhatap"),QStringLiteral("Uyarılar")});
    accounting_->setObjectName("accountingTable");
    accounting_->setSelectionMode(QAbstractItemView::ExtendedSelection);
    accounting_->setEditTriggers(QAbstractItemView::DoubleClicked|QAbstractItemView::EditKeyPressed);
    accounting_->setColumnWidth(2,290); accounting_->setColumnWidth(5,240); accounting_->setColumnWidth(7,280);
    source_=new QPlainTextEdit(review_splitter); source_->setReadOnly(true);
    source_->setPlaceholderText(QStringLiteral("Seçili satırın PDF metni ve önerilen zarf burada gösterilir."));
    review_layout->addWidget(review_splitter);
    tabs->addTab(review,QStringLiteral("Excel hazırlama / inceleme"));
    connect(accounting_,&QTableWidget::itemSelectionChanged,this,[this] {
        const int current=accounting_->currentRow();
        source_->setPlainText(current>=0 && current<static_cast<int>(accounting_rows_.size()) ?
            s(accounting_rows_[static_cast<std::size_t>(current)].source_text):QString{});
    });
    connect(accounting_,&QTableWidget::itemChanged,this,[this](QTableWidgetItem* item) {
        if (item->column()>0 && item->column()<=static_cast<int>(column_count)) {
            const QSignalBlocker blocker(accounting_);
            accounting_->item(item->row(),0)->setText(QStringLiteral("Değiştirildi — kaydedilmedi"));
            accounting_rows_[static_cast<std::size_t>(item->row())].approved=false;
            paintAccountingRow(item->row());
        }
    });
    connect(select_all,&QPushButton::clicked,accounting_,&QTableWidget::selectAll);
    connect(save,&QPushButton::clicked,this,[this]{reviewSelected(false);});
    connect(approve,&QPushButton::clicked,this,[this]{reviewSelected(true);});
    connect(apply_date,&QPushButton::clicked,this,[this] {
        for (const auto& index:accounting_->selectionModel()->selectedRows()) accounting_->item(index.row(),1)->setText(date_->text());
    });
    connect(draft,&QPushButton::clicked,this,[this]{exportExcel(true);});
    connect(export_button,&QPushButton::clicked,this,[this]{exportExcel(false);});
    connect(prepare,&QPushButton::clicked,this,[this] {
        if (selected_batch_.empty() || busy()) return;
        if(hasUnsavedEdits()){status_->setText(QStringLiteral("Önce düzenlediğiniz satırları kaydedin."));return;}
        const auto id=selected_batch_; setBusy(true);
        watcher_.setFuture(QtConcurrent::run([this,id] {
            try { workspace_.prepare_accounting(id); return Outcome{id,{}}; }
            catch(const Error& error){return Outcome{id,error.what()};}
            catch(...){return Outcome{id,"unexpected_error"};}
        }));
    });
    auto* changes = new QLabel(QStringLiteral("MUZ-9 — Borçlu başına satır ve renkli inceleme\n"
        "Her borçlu kendi kimlik numarasıyla ayrı satırda. Muhatap ayrı sütunda; durumlar yeşil, sarı ve kırmızı.\n\n"
        "MUZ-7 — Test sıfırlama ve eski kayıtların tebliğ tarihi\n"
        "Test düğmesi veritabanını temizler. Eski boş tarihler bugünün tarihiyle doldurulur ve yeniden onay bekler.\n\n"
        "MUZ-5 — Geçici tebliğ tarihi\n"
        "Yeni satırlarda bugünün tarihi kullanılır. KEP entegrasyonunda tarih KEP'ten alınacak.\n\n"
        "MUZ-3 — ZIP import, PDF review and accounting Excel export\n"
        "ZIP alımı, yerel PDF okuma, kaynak metinle toplu inceleme ve 11 sütunlu Excel çıktısı.\n\n"
        "MUZ-1 — Desktop skeleton and document import milestone\n"
        "Yerel klasör importu, SHA-256 arşivi, SQLite batch kayıtları ve inceleme listesi."), tabs);
    changes->setWordWrap(true);
    changes->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    tabs->addTab(changes, QStringLiteral("Neler değişti?"));
    layout->addWidget(splitter, 1);
    status_ = new QLabel(QStringLiteral("Hazır. Yeni batch oluşturmak için klasör seçin."), central);
    status_->setWordWrap(true);
    status_->setTextFormat(Qt::PlainText);
    layout->addWidget(status_);
    setCentralWidget(central);
    connect(import_, &QPushButton::clicked, this, [this] {
        const auto folder = QFileDialog::getExistingDirectory(this, QStringLiteral("İndirilen KEP belgelerinin klasörünü seçin"));
        if (folder.isEmpty()) return;
        const auto utf8 = folder.toUtf8();
        importFolder(std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(utf8.constData()),
            static_cast<std::size_t>(utf8.size()))));
    });
    connect(batches_, &QTableWidget::itemSelectionChanged, this, [this] { selectBatch(); });
    connect(&watcher_, &QFutureWatcher<Outcome>::finished, this, [this] {
        setBusy(false);
        const auto outcome = watcher_.result();
        if (!outcome.error.empty()) {
            refresh(outcome.batch_id);
            status_->setText(errorText(outcome.error));
        }
        else {
            status_->setText(QStringLiteral("İçe aktarma tamamlandı. Batch: ") + s(outcome.batch_id));
            refresh(outcome.batch_id);
        }
    });
    connect(&watcher_,&QFutureWatcher<Outcome>::finished,this,[this,tabs,review] {
        if(!accounting_rows_.empty())tabs->setCurrentWidget(review);
    });
    refresh();
}

MainWindow::~MainWindow() { watcher_.waitForFinished(); }

void MainWindow::importFolder(const std::filesystem::path& folder) {
    if (watcher_.isRunning()) return;
    if(hasUnsavedEdits()){status_->setText(QStringLiteral("Önce düzenlediğiniz satırları kaydedin."));return;}
    setBusy(true);
    status_->setText(QStringLiteral("Belgeler kopyalanıyor ve bütünlükleri doğrulanıyor…"));
    watcher_.setFuture(QtConcurrent::run([this, folder] {
        try { return Outcome{workspace_.import_folder(folder).batch.id, {}}; }
        catch (const Error& error) { return Outcome{{}, error.what()}; }
        catch (...) { return Outcome{{}, "unexpected_error"}; }
    }));
}

void MainWindow::refresh(const std::string& select_id) {
    try {
        const auto batches = workspace_.batches();
        const QSignalBlocker blocker(batches_);
        batches_->setRowCount(0);
        int selected = 0;
        for (const auto& batch : batches) {
            if (batch.id == select_id) selected = batches_->rowCount();
            row(batches_, {s(batch.id), s(batch.created_at), batch.status == "needs_review" ?
                QStringLiteral("İnceleme gerekli") : QStringLiteral("İçe aktarıldı"),
                QString::number(batch.imported_count), QString::number(batch.duplicate_count), QString::number(batch.issue_count)});
        }
        if (!batches.empty()) batches_->selectRow(selected);
        selectBatch();
    } catch (const Error& error) { status_->setText(errorText(error.what())); }
}

void MainWindow::selectBatch() {
    if(hasUnsavedEdits()) {
        status_->setText(QStringLiteral("Batch değiştirmeden önce düzenlediğiniz satırları kaydedin."));
        const QSignalBlocker blocker(batches_);
        for(int i=0;i<batches_->rowCount();++i)if(batches_->item(i,0)->text().toStdString()==selected_batch_)batches_->selectRow(i);
        return;
    }
    documents_->setRowCount(0);
    issues_->setRowCount(0);
    selected_batch_.clear(); accounting_rows_.clear(); showAccounting();
    const auto index = batches_->currentRow();
    if (index < 0) return;
    try {
        const auto id = batches_->item(index, 0)->text().toStdString();
        selected_batch_=id;
        for (const auto& doc : workspace_.documents(id))
            row(documents_, {s(doc.original_filename), s(doc.file.mime_type), QString::number(doc.file.size),
                doc.duplicate ? QStringLiteral("Evet") : QStringLiteral("Hayır"), s(doc.file.sha256)});
        for (const auto& issue : workspace_.issues(id)) row(issues_, {s(issue.source_path), errorText(issue.code)});
        accounting_rows_=workspace_.accounting_rows(id); showAccounting();
    } catch (const Error& error) { status_->setText(errorText(error.what())); }
}

void MainWindow::setBusy(bool value) {
    import_->setEnabled(!value); batches_->setEnabled(!value); accounting_->setEnabled(!value);
    for (auto* button:actions_) button->setEnabled(!value);
    progress_->setVisible(value);
}

void MainWindow::importArchive(const std::filesystem::path& archive) {
    if (busy()) return;
    if(hasUnsavedEdits()){status_->setText(QStringLiteral("Önce düzenlediğiniz satırları kaydedin."));return;}
    setBusy(true); status_->setText(QStringLiteral("ZIP arşivleniyor, PDF’ler okunuyor ve Excel satırları hazırlanıyor…"));
    watcher_.setFuture(QtConcurrent::run([this,archive] {
        std::string id;
        try {
            id=workspace_.import_archive(archive).batch.id;
            workspace_.prepare_accounting(id);
            return Outcome{id,{}};
        } catch(const Error& error){return Outcome{id,error.what()};}
        catch(...){return Outcome{id,"unexpected_error"};}
    }));
}

void MainWindow::showAccounting() {
    const QSignalBlocker blocker(accounting_);
    accounting_->setRowCount(0); source_->clear();
    for (const auto& record:accounting_rows_) {
        QStringList values{record.approved?QStringLiteral("Onaylı"):QStringLiteral("Onay bekliyor")};
        for (const auto& cell:record.cells) values.append(s(cell));
        QStringList warnings; for(const auto& warning:record.warnings) warnings.append(s(warning));
        values.append(s(recipient_text(record)));
        values.append(warnings.join("; ")); row(accounting_,values);
        const int index=accounting_->rowCount()-1;
        accounting_->item(index,0)->setFlags(accounting_->item(index,0)->flags() & ~Qt::ItemIsEditable);
        accounting_->item(index,11)->setFlags(accounting_->item(index,11)->flags() & ~Qt::ItemIsEditable);
        accounting_->item(index,12)->setFlags(accounting_->item(index,12)->flags() & ~Qt::ItemIsEditable);
        accounting_->item(index,0)->setToolTip(s(record.id)+"\n"+s(record.source_path));
        paintAccountingRow(index);
    }
}

void MainWindow::paintAccountingRow(int index) {
    const QSignalBlocker blocker(accounting_);
    auto record=accounting_rows_.at(static_cast<std::size_t>(index));
    for(std::size_t c=0;c<column_count;++c)record.cells[c]=accounting_->item(index,static_cast<int>(c)+1)->text().toStdString();
    const bool dirty=accounting_->item(index,0)->text().contains(QStringLiteral("kaydedilmedi"));
    auto state=review_status(record);
    if(dirty && state==ReviewStatus::ready)state=ReviewStatus::review;
    const auto color=state==ReviewStatus::blocked?QColor("#F8D7DA"):state==ReviewStatus::review?QColor("#FFF3CD"):QColor("#D4EDDA");
    const auto label=state==ReviewStatus::blocked?QStringLiteral("İlerlenemiyor"):state==ReviewStatus::review?
        QStringLiteral("İnceleme gerekli"):QStringLiteral("Sorunsuz");
    accounting_->item(index,0)->setText(label+" — "+(dirty?QStringLiteral("kaydedilmedi"):
        record.approved?QStringLiteral("Onaylı"):QStringLiteral("Onay bekliyor")));
    for(int c=0;c<accounting_->columnCount();++c) {
        accounting_->item(index,c)->setBackground(color);
        accounting_->item(index,c)->setForeground(QColor("#17202A"));
    }
}

void MainWindow::reviewSelected(bool approve) {
    try {
        std::vector<AccountingRow> edits;
        for (const auto& index:accounting_->selectionModel()->selectedRows()) {
            auto record=accounting_rows_.at(static_cast<std::size_t>(index.row()));
            for (std::size_t c=0;c<column_count;++c) record.cells[c]=accounting_->item(index.row(),static_cast<int>(c)+1)->text().toStdString();
            record.approved=approve; edits.push_back(std::move(record));
        }
        if (edits.empty()) {status_->setText(QStringLiteral("Önce incelemek istediğiniz satırları seçin."));return;}
        workspace_.review_accounting(selected_batch_,edits);
        // Refresh selected records only: other rows may contain unsaved edits.
        const auto saved=workspace_.accounting_rows(selected_batch_);
        const QSignalBlocker blocker(accounting_);
        for (const auto& edit : edits) {
            const auto record=std::find_if(saved.begin(),saved.end(),[&](const auto& value){return value.id==edit.id;});
            if (record==saved.end()) throw Error(ErrorCode::storage);
            for (std::size_t i=0; i<accounting_rows_.size(); ++i) {
                if (accounting_rows_[i].id!=edit.id) continue;
                accounting_rows_[i]=*record;
                accounting_->item(static_cast<int>(i),0)->setText(record->approved?QStringLiteral("Onaylı"):QStringLiteral("Onay bekliyor"));
                QStringList warnings; for(const auto& warning:record->warnings)warnings.append(s(warning));
                accounting_->item(static_cast<int>(i),11)->setText(s(recipient_text(*record)));
                accounting_->item(static_cast<int>(i),12)->setText(warnings.join("; "));
                paintAccountingRow(static_cast<int>(i));
            }
        }
        status_->setText(approve?QStringLiteral("Seçili satırlar onaylandı; Onaylı Excel ile dışa aktarabilirsiniz."):
            QStringLiteral("Seçili satırlardaki değişiklikler kaydedildi."));
    } catch(const Error&){status_->setText(QStringLiteral("Kayıt başarısız. Çelişkili zarf/muhatap satırlarını onaylamayın. Daire, esas, borçlu, Evet/Hayır, tarih (gg.aa.yyyy) ve tutarı (1234,56) kontrol edin."));}
}

void MainWindow::exportExcel(bool draft) {
    if (selected_batch_.empty()) return;
    for(int i=0;i<accounting_->rowCount();++i) if(accounting_->item(i,0)->text().contains(QStringLiteral("kaydedilmedi"))) {
        status_->setText(QStringLiteral("Önce düzenlediğiniz satırları kaydedin veya onaylayın."));return;
    }
    const auto output=QFileDialog::getSaveFileName(this,QStringLiteral("Excel çıktısı için yeni dosya adı seçin"),
        draft?"Icra_Dosyalari_Inceleme.xlsx":"Icra_Dosyalari.xlsx","Excel (*.xlsx)");
    if(output.isEmpty())return;
    try {
        workspace_.export_accounting(selected_batch_,native(output),draft);
        status_->setText(QStringLiteral("Excel oluşturuldu: ")+output);
    } catch(const Error&){status_->setText(QStringLiteral("Excel oluşturulamadı. Yeni bir .xlsx dosya adı kullanın; onaylı çıktı için önce satırları onaylayın."));}
}

void MainWindow::resetDatabaseForTesting() {
    if (busy()) return;
    const auto answer = QMessageBox::warning(this, QStringLiteral("Test veritabanını sıfırla"),
        QStringLiteral("Tüm işlem grupları, belgelerin veritabanı kayıtları, Excel satırları, onaylar ve işlem geçmişi silinecek. "
                       "Kaydedilmemiş değişiklikler de silinir. Kaynak ZIP/PDF ve oluşturulmuş Excel dosyaları diskte kalır.\n\n"
                       "Veritabanı sıfırlansın mı?"), QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (answer != QMessageBox::Yes) return;
    setBusy(true);
    try {
        workspace_.reset_database_for_testing();
        selected_batch_.clear(); accounting_rows_.clear(); showAccounting(); date_->clear();
        refresh();
        status_->setText(QStringLiteral("Test veritabanı sıfırlandı. ZIP'i yeniden içe aktarabilirsiniz."));
    } catch (const Error& error) { status_->setText(errorText(error.what())); }
    setBusy(false);
}

bool MainWindow::hasUnsavedEdits() const {
    if(!accounting_)return false;
    for(int i=0;i<accounting_->rowCount();++i)
        if(accounting_->item(i,0)->text().contains(QStringLiteral("kaydedilmedi")))return true;
    return false;
}
int MainWindow::batchCount() const { return batches_->rowCount(); }
int MainWindow::documentCount() const { return documents_->rowCount(); }
void MainWindow::closeEvent(QCloseEvent* event) {
    if (watcher_.isRunning()) {
        status_->setText(QStringLiteral("İçe aktarma sürüyor. Tamamlandığında pencereyi kapatabilirsiniz."));
        event->ignore();
    } else if(hasUnsavedEdits()) {
        status_->setText(QStringLiteral("Kapatmadan önce düzenlediğiniz satırları kaydedin."));event->ignore();
    } else QMainWindow::closeEvent(event);
}
} // namespace muz
