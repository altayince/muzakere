#pragma once
#include "muz/application/ports.hpp"
#include <QFutureWatcher>
#include <QMainWindow>

class QLabel;
class QPushButton;
class QTableWidget;
class QProgressBar;
class QCloseEvent;
class QPlainTextEdit;
class QLineEdit;

namespace muz {
class MainWindow final : public QMainWindow {
public:
    explicit MainWindow(Workspace& workspace);
    ~MainWindow() override;
    void importFolder(const std::filesystem::path& folder);
    void importArchive(const std::filesystem::path& archive);
    [[nodiscard]] bool busy() const { return watcher_.isRunning(); }
    [[nodiscard]] int batchCount() const;
    [[nodiscard]] int documentCount() const;
protected:
    void closeEvent(QCloseEvent* event) override;
private:
    struct Outcome { std::string batch_id; std::string error; };
    Workspace& workspace_;
    QTableWidget* batches_;
    QTableWidget* documents_;
    QTableWidget* issues_;
    QPushButton* import_;
    QLabel* status_;
    QProgressBar* progress_;
    QTableWidget* accounting_{};
    QPlainTextEdit* source_;
    QLineEdit* date_;
    std::vector<QPushButton*> actions_;
    std::vector<AccountingRow> accounting_rows_;
    std::string selected_batch_;
    QFutureWatcher<Outcome> watcher_;
    void refresh(const std::string& select_id = {});
    void selectBatch();
    void showAccounting();
    void paintAccountingRow(int index);
    void reviewSelected(bool approve);
    void exportExcel(bool draft);
    void resetDatabaseForTesting();
    void setBusy(bool busy);
    bool hasUnsavedEdits() const;
};
} // namespace muz
