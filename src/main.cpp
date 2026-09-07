#include "muz/infrastructure/local_workspace.hpp"
#include "main_window.hpp"

#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QMessageBox>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>

namespace {
std::filesystem::path path(const QString& value) {
    const auto bytes = value.toUtf8();
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(bytes.constData()),
        static_cast<std::size_t>(bytes.size())));
}
}

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    QApplication::setApplicationName("Muzakere");
    QApplication::setOrganizationName("Muzakere");
    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"workspace", "Local workspace directory", "path"});
    parser.addOption({"smoke-test", "Exercise desktop startup and background import using temporary synthetic data"});
    parser.addOption({"import-zip", "Import a ZIP and prepare accounting rows without opening the UI", "path"});
    parser.addOption({"export-draft", "Export the imported rows as a clearly marked review draft", "xlsx"});
    parser.process(app);
    const bool smoke = parser.isSet("smoke-test");
    QTemporaryDir temporary;
    const auto root = smoke ? temporary.path() + "/workspace" :
        parser.isSet("workspace") ? parser.value("workspace") :
        QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/workspace";
    try {
        if (smoke && !temporary.isValid()) return 2;
        muz::LocalWorkspace workspace(path(root));
        if (parser.isSet("import-zip")) {
            const auto batch=workspace.import_archive(path(parser.value("import-zip")));
            const auto rows=workspace.prepare_accounting(batch.batch.id);
            if(parser.isSet("export-draft")) workspace.export_accounting(batch.batch.id,path(parser.value("export-draft")),true);
            QFile output; output.open(stdout,QIODevice::WriteOnly);
            output.write(QJsonDocument(QJsonObject{{"batch_id",QString::fromStdString(batch.batch.id)},
                {"imported_documents",static_cast<int>(batch.batch.imported_count)},
                {"accounting_rows",static_cast<int>(rows.size())}}).toJson());
            return 0;
        }
        muz::MainWindow window(workspace);
        window.show();
        QTimer poll;
        if (smoke) {
            const auto input = temporary.path() + "/input";
            if (!QDir().mkpath(input)) return 2;
            QFile sample(input + "/synthetic.txt");
            if (!sample.open(QIODevice::WriteOnly) || sample.write("synthetic smoke fixture\n") < 0) return 2;
            sample.close();
            window.importFolder(path(input));
            QObject::connect(&poll, &QTimer::timeout, &app, [&] {
                if (!window.busy()) {
                    app.processEvents();
                    app.exit(window.batchCount() == 1 && window.documentCount() == 1 ? 0 : 3);
                }
            });
            poll.start(50);
            QTimer::singleShot(30000, &app, [&] { app.exit(4); });
        }
        return app.exec();
    } catch (const muz::Error& error) {
        if (!smoke && !parser.isSet("import-zip")) QMessageBox::critical(nullptr, QStringLiteral("Çalışma alanı açılamadı"),
            QStringLiteral("Hata kodu: ") + QString::fromUtf8(error.what()));
        return 1;
    } catch (...) {
        if (!smoke && !parser.isSet("import-zip")) QMessageBox::critical(nullptr, QStringLiteral("Uygulama başlatılamadı"), QStringLiteral("Beklenmeyen başlatma hatası."));
        return 1;
    }
}
