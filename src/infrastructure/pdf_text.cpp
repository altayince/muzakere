#include "accounting_adapters.hpp"
#include <QCoreApplication>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>

namespace muz {
PdfText extract_pdf(const QByteArray& bytes) {
    QProcess worker;
#ifdef Q_OS_WIN
    worker.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= 0x08000000; // CREATE_NO_WINDOW for the local worker.
    });
#endif
    worker.start(QCoreApplication::applicationDirPath() + "/muz_pdf_worker", {});
    if (!worker.waitForStarted(5000)) return {{}, "PDF okuyucu başlatılamadı"};
    worker.write(bytes);
    worker.closeWriteChannel();
    if (!worker.waitForFinished(30000)) {
        worker.kill(); worker.waitForFinished(5000);
        return {{}, "PDF okuma zaman aşımı; manuel inceleme gerekli"};
    }
    const auto output = worker.readAllStandardOutput();
    if (worker.exitStatus() != QProcess::NormalExit || worker.exitCode() != 0 || output.size() > 16 * 1024 * 1024)
        return {{}, "PDF okunamadı; manuel inceleme gerekli"};
    QJsonParseError error;
    const auto result = QJsonDocument::fromJson(output, &error);
    if (error.error != QJsonParseError::NoError || !result.isObject()) return {{}, "Geçersiz PDF okuma sonucu"};
    return {result.object().value("text").toString(), result.object().value("error").toString().toStdString()};
}
} // namespace muz
