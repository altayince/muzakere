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
    layout->addWidget(new QLabel(QStringLiteral("Belgeler yerel arşive kopyalanır. Dosya eşleştirme ve hukuki onay henüz yapılmaz."), central));
    import_ = new QPushButton(QStringLiteral("Klasörden &belge al…  (Ctrl+I)"), central);
    import_->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_I));
    layout->addWidget(import_);
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
    auto* changes = new QLabel(QStringLiteral("MUZ-1 — Desktop skeleton and document import milestone\n"
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
        import_->setEnabled(true);
        progress_->hide();
        const auto outcome = watcher_.result();
        if (!outcome.error.empty()) status_->setText(errorText(outcome.error));
        else {
            status_->setText(QStringLiteral("İçe aktarma tamamlandı. Batch: ") + s(outcome.batch_id));
            refresh(outcome.batch_id);
        }
    });
    refresh();
}

MainWindow::~MainWindow() { watcher_.waitForFinished(); }

void MainWindow::importFolder(const std::filesystem::path& folder) {
    if (watcher_.isRunning()) return;
    import_->setEnabled(false);
    progress_->show();
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
    documents_->setRowCount(0);
    issues_->setRowCount(0);
    const auto index = batches_->currentRow();
    if (index < 0) return;
    try {
        const auto id = batches_->item(index, 0)->text().toStdString();
        for (const auto& doc : workspace_.documents(id))
            row(documents_, {s(doc.original_filename), s(doc.file.mime_type), QString::number(doc.file.size),
                doc.duplicate ? QStringLiteral("Evet") : QStringLiteral("Hayır"), s(doc.file.sha256)});
        for (const auto& issue : workspace_.issues(id)) row(issues_, {s(issue.source_path), errorText(issue.code)});
    } catch (const Error& error) { status_->setText(errorText(error.what())); }
}

int MainWindow::batchCount() const { return batches_->rowCount(); }
int MainWindow::documentCount() const { return documents_->rowCount(); }
void MainWindow::closeEvent(QCloseEvent* event) {
    if (watcher_.isRunning()) {
        status_->setText(QStringLiteral("İçe aktarma sürüyor. Tamamlandığında pencereyi kapatabilirsiniz."));
        event->ignore();
    } else QMainWindow::closeEvent(event);
}
} // namespace muz
