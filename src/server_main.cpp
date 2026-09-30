#include "muz/infrastructure/local_workspace.hpp"
#include "muz/web/http_server.hpp"

#include <QCommandLineParser>
#include <QGuiApplication>
#include <QFontDatabase>
#include <QEventLoop>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>

namespace {
std::filesystem::path path(const QString& value) {
    const auto bytes = value.toUtf8();
    return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(bytes.constData()),
        static_cast<std::size_t>(bytes.size())));
}
}

int main(int argc, char* argv[]) {
    // QTextDocument/QPdfWriter need the GUI font infrastructure even without
    // windows or a display server. Keep the HTTP process headless by default.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
#ifdef Q_OS_WIN
    // Match the desktop CLI's offscreen font setup so PDFs contain real glyphs.
    for (const auto* font : {"times.ttf", "timesbd.ttf", "timesi.ttf", "timesbi.ttf", "arial.ttf"})
        QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR") + "/Fonts/" + font);
#endif
    QCoreApplication::setApplicationName("Muzakere Server");
    QCoreApplication::setOrganizationName("Muzakere");

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addOption({"workspace", "Workspace directory", "path"});
    parser.addOption({"host", "Listen host", "host", "127.0.0.1"});
    parser.addOption({"port", "Listen port", "port", "8080"});
    parser.addOption({"smoke-test", "Start the server and verify the health endpoint"});
    parser.process(app);

    std::unique_ptr<QTemporaryDir> smoke_workspace;
    QString workspace_root;
    if (parser.isSet("workspace")) {
        workspace_root = parser.value("workspace");
    } else if (parser.isSet("smoke-test")) {
        smoke_workspace = std::make_unique<QTemporaryDir>();
        if (!smoke_workspace->isValid()) return 2;
        workspace_root = smoke_workspace->path() + "/workspace";
    } else {
        workspace_root = qEnvironmentVariable("MUZ_WORKSPACE",
            QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/web-workspace");
    }
    bool ok = false;
    const auto requested_port = parser.value("port").toUShort(&ok);
    if (!ok) return 2;

    try {
        muz::LocalWorkspace workspace(path(workspace_root));
        muz::WebServer server(workspace);
        if (!server.listen(QHostAddress(parser.value("host")), requested_port)) return 2;
        qInfo("Muzakere HTTP port: %u", static_cast<unsigned>(server.port()));
        if (parser.isSet("smoke-test")) {
            QNetworkAccessManager network;
            QEventLoop loop;
            auto* reply = network.get(QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1/health").arg(server.port()))));
            QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
            QTimer::singleShot(5000, &loop, &QEventLoop::quit);
            loop.exec();
            const auto success = reply->isFinished() &&
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
            reply->deleteLater();
            return success ? 0 : 3;
        }
        return app.exec();
    } catch (const muz::Error&) {
        return 1;
    }
}
