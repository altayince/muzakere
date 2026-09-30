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
#include <xlsxdocument.h>
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
    QByteArray content_type;
    QByteArray content_disposition;
};

HttpResult request(QNetworkAccessManager& network, const QNetworkRequest& request, const QByteArray& body = {}) {
    QEventLoop loop;
    auto* reply = body.isNull() ? network.get(request) : network.post(request, body);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    QTimer::singleShot(15000, &loop, &QEventLoop::quit);
    loop.exec();
    REQUIRE(reply->isFinished());
    HttpResult result{reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), reply->readAll(),
        reply->rawHeader("Content-Type"), reply->rawHeader("Content-Disposition")};
    reply->deleteLater();
    return result;
}

QJsonObject post_json(QNetworkAccessManager& network, const QUrl& url, const QJsonObject& object, int expected_status = 200) {
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    const auto result = ::request(network, request, QJsonDocument(object).toJson(QJsonDocument::Compact));
    INFO(result.body.constData());
    REQUIRE(result.status == expected_status);
    return QJsonDocument::fromJson(result.body).object();
}

QString content_case(const QString& case_number, const QString& debtor_id) {
    auto text = content();
    text.replace(QStringLiteral("2026/42"), case_number);
    text.replace(QStringLiteral("00000000000"), debtor_id);
    return text;
}

QString envelope_case(const QString& case_number) {
    auto text = envelope();
    text.replace(QStringLiteral("2026/42"), case_number);
    return text;
}

QJsonObject upload_archive(QNetworkAccessManager& network, quint16 port,
        const std::vector<std::pair<std::string, QByteArray>>& entries) {
    QNetworkRequest upload(QUrl(QStringLiteral("http://127.0.0.1:%1/api/imports").arg(port)));
    const auto boundary = QByteArray("----MuzWebTestBoundary");
    upload.setHeader(QNetworkRequest::ContentTypeHeader, "multipart/form-data; boundary=" + boundary);
    const auto archive = zip(entries);
    QByteArray body = "--" + boundary +
        "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"input.zip\"\r\n"
        "Content-Type: application/zip\r\n\r\n" + archive + "\r\n--" + boundary + "--\r\n";
    const auto result = request(network, upload, body);
    INFO(result.body.constData());
    REQUIRE(result.status == 201);
    return QJsonDocument::fromJson(result.body).object();
}

QJsonObject upload_review(QNetworkAccessManager& network, quint16 port) {
    return upload_archive(network, port, {{"a/content.pdf", pdf(content())}, {"a/envelope.pdf", pdf(envelope())}});
}

QJsonObject get_json(QNetworkAccessManager& network, const QUrl& url, int expected_status = 200) {
    const auto result = request(network, QNetworkRequest(url));
    INFO(result.body.constData());
    REQUIRE(result.status == expected_status);
    return QJsonDocument::fromJson(result.body).object();
}

QJsonObject first_row(const QJsonObject& review) {
    return review["rows"].toArray().first().toObject();
}

QJsonArray row_ids(const QJsonObject& review) {
    QJsonArray ids;
    for (const auto& value : review["rows"].toArray()) ids.append(value.toObject()["id"].toString());
    return ids;
}

QJsonObject approve_all(QNetworkAccessManager& network, quint16 port, const QJsonObject& review) {
    return post_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/batches/%2/review/approve")
        .arg(port).arg(review["batch"].toObject()["id"].toString())), {{"rowIds", row_ids(review)}});
}

QString save_xlsx(const QByteArray& bytes, QTemporaryDir& temp, const QString& name = QStringLiteral("download.xlsx")) {
    const auto path = temp.path() + '/' + name;
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    REQUIRE(file.write(bytes) == bytes.size());
    file.close();
    return path;
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
    const auto object = upload_review(network, server.port());
    REQUIRE(object["batch"].toObject()["importedCount"].toInt() == 2);
    REQUIRE(object["summary"].toObject()["total"].toInt() == 1);
    const auto row = first_row(object);
    REQUIRE(row["status"].toString() == "review");
    REQUIRE(row["color"].toString() == "yellow");
    REQUIRE(row["cells"].toObject()["caseNumber"].toString() == "2026/42");
    REQUIRE(row["cells"].toObject()["debtorId"].toString() == "00000000000");
}

TEST_CASE("Web review rows can be fetched, edited, saved and explicitly approved", "[integration][web]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    muz::LocalWorkspace workspace(std::filesystem::path(temp.path().toStdString()) / "workspace");
    muz::WebServer server(workspace);
    REQUIRE(server.listen(QHostAddress::LocalHost, 0));

    QNetworkAccessManager network;
    auto review = upload_review(network, server.port());
    const auto batch_id = review["batch"].toObject()["id"].toString();
    const auto row_id = first_row(review)["id"].toString();
    review = get_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/batches/%2/review").arg(server.port()).arg(batch_id)));
    REQUIRE(first_row(review)["id"].toString() == row_id);

    auto row = first_row(review);
    auto cells = row["cells"].toObject();
    cells["amount"] = "2.000,00";
    cells["debtor"] = "WEB BORCLU";
    row["cells"] = cells;
    review = post_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/batches/%2/review/save").arg(server.port()).arg(batch_id)),
        {{"rows", QJsonArray{row}}});
    auto saved = first_row(review);
    REQUIRE(saved["approved"].toBool() == false);
    REQUIRE(saved["cells"].toObject()["amount"].toString() == "2.000,00");
    REQUIRE(saved["cells"].toObject()["debtor"].toString() == "WEB BORCLU");

    review = get_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/batches/%2/review").arg(server.port()).arg(batch_id)));
    REQUIRE(first_row(review)["cells"].toObject()["amount"].toString() == "2.000,00");
    REQUIRE(first_row(review)["approved"].toBool() == false);

    review = post_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/batches/%2/review/approve").arg(server.port()).arg(batch_id)),
        {{"rowIds", QJsonArray{row_id}}});
    REQUIRE(first_row(review)["approved"].toBool());
    review = get_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/batches/%2/review").arg(server.port()).arg(batch_id)));
    REQUIRE(first_row(review)["approved"].toBool());
}

TEST_CASE("Web review validation state comes from C++ save and blocked rows cannot be approved", "[integration][web]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    muz::LocalWorkspace workspace(std::filesystem::path(temp.path().toStdString()) / "workspace");
    muz::WebServer server(workspace);
    REQUIRE(server.listen(QHostAddress::LocalHost, 0));

    QNetworkAccessManager network;
    auto review = upload_review(network, server.port());
    const auto batch_id = review["batch"].toObject()["id"].toString();
    auto row = first_row(review);
    const auto row_id = row["id"].toString();
    auto cells = row["cells"].toObject();
    cells["debtorId"] = "bad-id";
    row["cells"] = cells;

    review = post_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/batches/%2/review/save").arg(server.port()).arg(batch_id)),
        {{"rows", QJsonArray{row}}});
    REQUIRE(first_row(review)["status"].toString() == "blocked");
    REQUIRE(first_row(review)["color"].toString() == "red");
    REQUIRE(first_row(review)["blockers"].toArray().contains("approval_requirements_not_met"));

    const auto failed = post_json(network,
        QUrl(QStringLiteral("http://127.0.0.1:%1/api/batches/%2/review/approve").arg(server.port()).arg(batch_id)),
        {{"rowIds", QJsonArray{row_id}}}, 400);
    REQUIRE(failed["error"].toObject()["code"].toString() == "approval_failed");
    review = get_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/batches/%2/review").arg(server.port()).arg(batch_id)));
    REQUIRE(first_row(review)["approved"].toBool() == false);
}

TEST_CASE("Web review API rejects malformed payloads and unknown ids", "[integration][web]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    muz::LocalWorkspace workspace(std::filesystem::path(temp.path().toStdString()) / "workspace");
    muz::WebServer server(workspace);
    REQUIRE(server.listen(QHostAddress::LocalHost, 0));

    QNetworkAccessManager network;
    auto review = upload_review(network, server.port());
    const auto batch_id = review["batch"].toObject()["id"].toString();
    auto row = first_row(review);
    row["id"] = "missing-row";
    REQUIRE(get_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/batches/00000000-0000-0000-0000-000000000000/review")
        .arg(server.port())), 404)["error"].toObject()["code"].toString() == "batch_not_found");
    REQUIRE(post_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/batches/%2/review/save").arg(server.port()).arg(batch_id)),
        {{"rows", QJsonArray{row}}}, 404)["error"].toObject()["code"].toString() == "row_not_found");
    REQUIRE(post_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/batches/%2/review/save").arg(server.port()).arg(batch_id)),
        {{"rows", "bad"}}, 400)["error"].toObject()["code"].toString() == "bad_payload");
    REQUIRE(post_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/batches/%2/review/approve").arg(server.port()).arg(batch_id)),
        {{"rowIds", QJsonArray{42}}}, 400)["error"].toObject()["code"].toString() == "bad_row_id");
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

TEST_CASE("Web HAMDATA export requires approval and downloads a valid workbook", "[integration][web][hamdata]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    muz::LocalWorkspace workspace(std::filesystem::path(temp.path().toStdString()) / "workspace");
    muz::WebServer server(workspace);
    REQUIRE(server.listen(QHostAddress::LocalHost, 0));

    QNetworkAccessManager network;
    auto review = upload_review(network, server.port());
    const auto batch_id = review["batch"].toObject()["id"].toString();

    auto failed = post_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/batches/%2/exports/hamdata")
        .arg(server.port()).arg(batch_id)), {}, 400);
    REQUIRE(failed["error"].toObject()["code"].toString() == "export_blocked");

    review = approve_all(network, server.port(), review);
    const auto created = post_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/batches/%2/exports/hamdata")
        .arg(server.port()).arg(batch_id)), {}, 201);
    REQUIRE(created["exportId"].toString().size() == 36);
    REQUIRE(created["downloadUrl"].toString().startsWith("/api/exports/"));
    REQUIRE(created["filename"].toString().startsWith("HAMDATA-"));

    const auto download = request(network, QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1%2")
        .arg(server.port()).arg(created["downloadUrl"].toString()))));
    REQUIRE(download.status == 200);
    REQUIRE(download.content_type == "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet");
    REQUIRE(download.content_disposition.contains("attachment"));
    REQUIRE(download.content_disposition.contains(created["filename"].toString().toUtf8()));

    const auto workbook = save_xlsx(download.body, temp, QStringLiteral("hamdata.xlsx"));
    QXlsx::Document book(workbook);
    REQUIRE(book.load());
    REQUIRE(book.sheetNames().contains("HAMDATA"));
    REQUIRE_FALSE(book.sheetNames().contains("MUHASEBE"));
    REQUIRE(book.sheetNames().contains("_MUZ"));
    REQUIRE(book.selectSheet("HAMDATA"));
    REQUIRE(book.dimension().lastRow() == 2);
    REQUIRE(book.dimension().lastColumn() == 11);
    REQUIRE(book.read(2, 7).toString() == "2026/42");
}

TEST_CASE("Web HAMDATA with accounting preserves HAMDATA rows and deduplicates MUHASEBE by identity", "[integration][web][hamdata]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    muz::LocalWorkspace workspace(std::filesystem::path(temp.path().toStdString()) / "workspace");
    muz::WebServer server(workspace);
    REQUIRE(server.listen(QHostAddress::LocalHost, 0));

    QNetworkAccessManager network;
    auto review = upload_archive(network, server.port(), {
        {"a/content.pdf", pdf(content_case(QStringLiteral("2026/42"), QStringLiteral("11111111111")))},
        {"a/envelope.pdf", pdf(envelope_case(QStringLiteral("2026/42")))},
        {"b/content.pdf", pdf(content_case(QStringLiteral("2026/43"), QStringLiteral("11111111111")))},
        {"b/envelope.pdf", pdf(envelope_case(QStringLiteral("2026/43")))},
        {"c/content.pdf", pdf(content_case(QStringLiteral("2026/44"), QStringLiteral("22222222222")))},
        {"c/envelope.pdf", pdf(envelope_case(QStringLiteral("2026/44")))}
    });
    REQUIRE(review["summary"].toObject()["total"].toInt() == 3);
    const auto batch_id = review["batch"].toObject()["id"].toString();
    review = approve_all(network, server.port(), review);

    const auto created = post_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/batches/%2/exports/hamdata-with-accounting")
        .arg(server.port()).arg(batch_id)), {}, 201);
    REQUIRE(created["filename"].toString().startsWith("HAMDATA-MUHASEBE-"));
    const auto download = request(network, QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1%2")
        .arg(server.port()).arg(created["downloadUrl"].toString()))));
    REQUIRE(download.status == 200);

    const auto workbook = save_xlsx(download.body, temp, QStringLiteral("hamdata-accounting.xlsx"));
    QXlsx::Document book(workbook);
    REQUIRE(book.load());
    REQUIRE(book.sheetNames().contains("MUHASEBE"));
    REQUIRE(book.sheetNames().contains("HAMDATA"));
    REQUIRE(book.sheetNames().contains("_MUZ"));

    REQUIRE(book.selectSheet("HAMDATA"));
    REQUIRE(book.dimension().lastRow() == 4);
    REQUIRE(book.dimension().lastColumn() == 11);
    REQUIRE(book.read(2, 7).toString() == "2026/42");
    REQUIRE(book.read(3, 7).toString() == "2026/43");
    REQUIRE(book.read(4, 7).toString() == "2026/44");

    REQUIRE(book.selectSheet("MUHASEBE"));
    REQUIRE(book.dimension().lastRow() == 3);
    REQUIRE(book.read(2, 1).toString() == "11111111111");
    REQUIRE(book.read(3, 1).toString() == "22222222222");
    REQUIRE(book.read(2, 4).toString().isEmpty());
    REQUIRE(book.read(3, 4).toString().isEmpty());
}

TEST_CASE("Web HAMDATA export reports blocked and unknown ids without exposing paths", "[integration][web][hamdata]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    muz::LocalWorkspace workspace(std::filesystem::path(temp.path().toStdString()) / "workspace");
    muz::WebServer server(workspace);
    REQUIRE(server.listen(QHostAddress::LocalHost, 0));

    QNetworkAccessManager network;
    auto review = upload_review(network, server.port());
    const auto batch_id = review["batch"].toObject()["id"].toString();
    auto row = first_row(review);
    auto cells = row["cells"].toObject();
    cells["debtorId"] = "bad-id";
    row["cells"] = cells;
    review = post_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/batches/%2/review/save")
        .arg(server.port()).arg(batch_id)), {{"rows", QJsonArray{row}}});
    REQUIRE(first_row(review)["status"].toString() == "blocked");

    const auto blocked = post_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/batches/%2/exports/hamdata")
        .arg(server.port()).arg(batch_id)), {}, 400);
    REQUIRE(blocked["error"].toObject()["code"].toString() == "export_blocked");
    REQUIRE_FALSE(QJsonDocument(blocked).toJson(QJsonDocument::Compact).contains(temp.path().toUtf8()));

    const auto unknown_batch = post_json(network,
        QUrl(QStringLiteral("http://127.0.0.1:%1/api/batches/00000000-0000-0000-0000-000000000000/exports/hamdata")
        .arg(server.port())), {}, 404);
    REQUIRE(unknown_batch["error"].toObject()["code"].toString() == "batch_not_found");

    const auto unknown_export = get_json(network,
        QUrl(QStringLiteral("http://127.0.0.1:%1/api/exports/00000000-0000-0000-0000-000000000000/download")
        .arg(server.port())), 404);
    REQUIRE(unknown_export["error"].toObject()["code"].toString() == "export_not_found");
}
