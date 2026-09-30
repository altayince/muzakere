#include "muz/infrastructure/local_workspace.hpp"
#include "muz/web/http_server.hpp"

#include <catch2/catch_test_macros.hpp>
#include <QBuffer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QPainter>
#include <QPdfWriter>
#include <QTemporaryDir>
#include <QTimer>
#include <miniz.h>

namespace {
QByteArray pdf(const QString& text) {
    QBuffer buffer;
    REQUIRE(buffer.open(QIODevice::WriteOnly));
    {
        QPdfWriter writer(&buffer);
        writer.setResolution(96);
        QPainter painter(&writer);
        painter.setFont(QFont("Arial", 9));
        painter.drawText(QRect(0, 0, 700, 1000), Qt::TextWordWrap, text);
    }
    return buffer.data();
}

QByteArray zip(const std::vector<std::pair<std::string, QByteArray>>& entries) {
    mz_zip_archive writer{};
    REQUIRE(mz_zip_writer_init_heap(&writer, 0, 0));
    for (const auto& [name, data] : entries)
        REQUIRE(mz_zip_writer_add_mem(&writer, name.c_str(), data.constData(), static_cast<size_t>(data.size()),
            MZ_DEFAULT_COMPRESSION));
    void* bytes = nullptr;
    size_t size = 0;
    REQUIRE(mz_zip_writer_finalize_heap_archive(&writer, &bytes, &size));
    QByteArray result(static_cast<const char*>(bytes), static_cast<qsizetype>(size));
    mz_free(bytes);
    mz_zip_writer_end(&writer);
    return result;
}

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

struct HttpResult {
    int status{};
    QByteArray body;
};

HttpResult request(QNetworkAccessManager& network, const QNetworkRequest& request, const QByteArray& body = {}) {
    QEventLoop loop;
    auto* reply = body.isNull() ? network.get(request) : network.post(request, body);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    loop.exec();
    REQUIRE(reply->isFinished());
    HttpResult result{reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), reply->readAll()};
    reply->deleteLater();
    return result;
}
} // namespace

TEST_CASE("Web server starts and answers health and static UI", "[integration][web]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    muz::LocalWorkspace workspace(std::filesystem::path(temp.path().toStdString()) / "workspace");
    muz::WebServer server(workspace);
    REQUIRE(server.listen(QHostAddress::LocalHost, 0));

    QNetworkAccessManager network;
    const auto health = request(network, QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1/health").arg(server.port()))));
    REQUIRE(health.status == 200);
    REQUIRE(QJsonDocument::fromJson(health.body).object()["status"].toString() == "ok");

    const auto page = request(network, QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1/").arg(server.port()))));
    REQUIRE(page.status == 200);
    REQUIRE(page.body.contains("ZIP"));
}

TEST_CASE("Web ZIP import uses existing parser and serializes review rows", "[integration][web]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    muz::LocalWorkspace workspace(std::filesystem::path(temp.path().toStdString()) / "workspace");
    muz::WebServer server(workspace);
    REQUIRE(server.listen(QHostAddress::LocalHost, 0));

    QNetworkAccessManager network;
    QNetworkRequest upload(QUrl(QStringLiteral("http://127.0.0.1:%1/api/imports").arg(server.port())));
    const auto boundary = QByteArray("----MuzWebTestBoundary");
    upload.setHeader(QNetworkRequest::ContentTypeHeader, "multipart/form-data; boundary=" + boundary);
    const auto archive = zip({{"a/content.pdf", pdf(content())}, {"a/envelope.pdf", pdf(envelope())}});
    QByteArray body = "--" + boundary +
        "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"input.zip\"\r\n"
        "Content-Type: application/zip\r\n\r\n" + archive + "\r\n--" + boundary + "--\r\n";
    const auto result = request(network, upload, body);
    INFO(result.body.constData());
    REQUIRE(result.status == 201);
    const auto object = QJsonDocument::fromJson(result.body).object();
    REQUIRE(object["batch"].toObject()["importedCount"].toInt() == 2);
    REQUIRE(object["summary"].toObject()["total"].toInt() == 1);
    const auto row = object["rows"].toArray().first().toObject();
    REQUIRE(row["status"].toString() == "review");
    REQUIRE(row["color"].toString() == "yellow");
    REQUIRE(row["cells"].toObject()["caseNumber"].toString() == "2026/42");
    REQUIRE(row["cells"].toObject()["debtorId"].toString() == "00000000000");
}

TEST_CASE("Web upload rejects non ZIP media before reaching workspace", "[integration][web]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    muz::LocalWorkspace workspace(std::filesystem::path(temp.path().toStdString()) / "workspace");
    muz::WebServer server(workspace);
    REQUIRE(server.listen(QHostAddress::LocalHost, 0));

    QNetworkAccessManager network;
    QNetworkRequest upload(QUrl(QStringLiteral("http://127.0.0.1:%1/api/imports").arg(server.port())));
    upload.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain");
    const auto result = request(network, upload, "not a zip");
    REQUIRE(result.status == 415);
    REQUIRE(QJsonDocument::fromJson(result.body).object()["error"].toObject()["code"].toString() == "unsupported_media_type");
}
