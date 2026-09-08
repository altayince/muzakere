#pragma once
#include "muz/application/ports.hpp"
#include <QWidget>
#include <QFutureWatcher>
#include <functional>

class QTableWidget; class QLabel; class QLineEdit; class QTextBrowser; class QPushButton;
namespace muz {
class ResponsePanel final : public QWidget {
public:
    ResponsePanel(Workspace& workspace,std::function<void(bool)> activity,QWidget* parent=nullptr);
    ~ResponsePanel() override;
    bool busy() const {return watcher_.isRunning();}
    void reload(const std::string& selected={});
    void importWorkbook(const std::filesystem::path& input);
    void generatePdfs(const std::filesystem::path& output);
private:
    struct Outcome {std::string return_id;std::string error;std::string output;};
    Workspace& workspace_;
    std::function<void(bool)> activity_;
    QFutureWatcher<Outcome> watcher_;
    QTableWidget* batches_;
    QTableWidget* table_;
    QTextBrowser* preview_;
    QLineEdit* lawyer_;
    QLineEdit* address_;
    QLabel* status_;
    std::vector<QPushButton*> buttons_;
    std::vector<ResponseRow> rows_;
    std::string selected_;
    std::string output_;
    void showRows();
    void showPreview();
    void setBusy(bool value);
    ResponseProfile profile() const;
};
} // namespace muz
