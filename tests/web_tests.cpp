#include "muz/infrastructure/local_workspace.hpp"
#include "muz/web/http_server.hpp"
#include "accounting_adapters.hpp"
#include "response_adapters.hpp"
#include "qt_paths.hpp"

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
#include <QUuid>
#include <xlsxcellformula.h>
#include <xlsxdocument.h>
#include <xlsxworksheet.h>
#include <functional>
#include <miniz.h>
#include <tuple>

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

QString save_xlsx(const QByteArray& bytes, QTemporaryDir& temp, const QString& name = QStringLiteral("download.xlsx")) {
    const auto path = temp.path() + '/' + name;
    QFile file(path);
    REQUIRE(file.open(QIODevice::WriteOnly | QIODevice::NewOnly));
    REQUIRE(file.write(bytes) == bytes.size());
    file.close();
    return path;
}

QByteArray file_bytes(const QString& path) {
    QFile file(path);
    REQUIRE(file.open(QIODevice::ReadOnly));
    return file.readAll();
}

std::vector<muz::AccountingRow> response_rows_sample() {
    std::vector<muz::AccountingRow> rows;
    const std::vector<std::tuple<std::string, std::string, std::string>> data{
        {"row-a1", "11111111111", "AYNI AD"},
        {"row-a2", "11111111111", "AYNI AD"},
        {"row-b", "22222222222", "AYNI AD"},
        {"row-c", "33333333333", "SIFIR BORCLU"},
        {"row-d", "44444444444", "EKSI BORCLU"}
    };
    int index = 1;
    for (const auto& [id, debtor_id, debtor_name] : data) {
        muz::AccountingRow row;
        row.id = id;
        row.document_id = id + "-doc";
        row.batch_id = "web-return";
        row.envelope_id = id + "-envelope";
        row.source_path = id + ".pdf";
        row.sha256 = std::string(64, 'a');
        row.recipient = "TEST MUHATAP";
        row.approved = true;
        row.cells = {"07.09.2026", "ANKARA 8. GENEL ICRA DAIRESI", "2026/" + std::to_string(index), "1234,56",
            debtor_name, debtor_id, "TEST ALACAKLI", "TR000000000000000000000000", "Evet", ""};
        rows.push_back(row);
        ++index;
    }
    return rows;
}

QByteArray returned_hamdata_workbook(QTemporaryDir& temp, const std::function<void(QXlsx::Document&)>& edit) {
    const auto path = temp.path() + "/returned-source.xlsx";
    muz::write_hamdata_xlsx(response_rows_sample(), muz::native_path(path), true);
    QXlsx::Document book(path);
    REQUIRE(book.load());
    edit(book);
    const auto returned = temp.path() + '/' + QUuid::createUuid().toString(QUuid::WithoutBraces) + ".xlsx";
    REQUIRE(book.saveAs(returned));
    return file_bytes(returned);
}

QJsonObject upload_return(QNetworkAccessManager& network, quint16 port, const QByteArray& workbook,
        int expected_status = 201, const QByteArray& field_name = "file",
        const QByteArray& content_type = "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet") {
    QNetworkRequest upload(QUrl(QStringLiteral("http://127.0.0.1:%1/api/accounting-returns").arg(port)));
    const auto boundary = QByteArray("----MuzReturnBoundary");
    upload.setHeader(QNetworkRequest::ContentTypeHeader, "multipart/form-data; boundary=" + boundary);
    QByteArray body = "--" + boundary +
        "\r\nContent-Disposition: form-data; name=\"" + field_name + "\"; filename=\"return.xlsx\"\r\n"
        "Content-Type: " + content_type + "\r\n\r\n" + workbook + "\r\n--" + boundary + "--\r\n";
    const auto result = request(network, upload, body);
    INFO(result.body.constData());
    REQUIRE(result.status == expected_status);
    return QJsonDocument::fromJson(result.body).object();
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
    REQUIRE(page.body.contains("stage-import"));
    REQUIRE(page.body.contains("stage-accounting"));
    REQUIRE(page.body.contains("stage-responses"));
    REQUIRE(page.body.contains("previewSelectedResponse"));
    REQUIRE(page.body.contains("responsePreviewPanel"));
    REQUIRE(page.body.contains("clearResponsePreview"));

    const auto script = request(network, QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1/app.js").arg(server.port()))));
    REQUIRE(script.status == 200);
    REQUIRE(script.body.contains("decision-blocked"));
    REQUIRE(script.body.contains("showResponseDownloads"));
    REQUIRE(script.body.contains("previewResponse"));
    REQUIRE(script.body.contains("/preview"));
    REQUIRE(script.body.contains("method: \"POST\""));
    REQUIRE(script.body.contains("lawyer: responseLawyer.value"));
    REQUIRE(script.body.contains("address: responseAddress.value"));
    REQUIRE(script.body.contains("catch (error)"));
    REQUIRE(script.body.count("if (currentPreviewRowId !== rowId) return;") >= 2);
    REQUIRE(script.body.contains("preview.disabled = item.status === \"blocked\""));
    REQUIRE(script.body.contains("input.checked = !input.disabled"));
    REQUIRE(script.body.contains("clearResponseDownloads();"));
    REQUIRE(script.body.contains("clearResponsePreview();"));
    REQUIRE(script.body.contains(QByteArray("\xE2\x80\x94")));

    const auto styles = request(network, QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1/styles.css").arg(server.port()))));
    REQUIRE(styles.status == 200);
    REQUIRE(styles.body.contains("preview-panel"));
    REQUIRE(styles.body.contains("response-workbench"));
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

TEST_CASE("Web HAMDATA export follows core exporter behavior without requiring approval", "[integration][web][hamdata]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    muz::LocalWorkspace workspace(std::filesystem::path(temp.path().toStdString()) / "workspace");
    muz::WebServer server(workspace);
    REQUIRE(server.listen(QHostAddress::LocalHost, 0));

    QNetworkAccessManager network;
    auto review = upload_review(network, server.port());
    const auto batch_id = review["batch"].toObject()["id"].toString();
    REQUIRE_FALSE(first_row(review)["approved"].toBool());
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

    const auto core_path = temp.path() + "/core-hamdata.xlsx";
    workspace.export_hamdata(batch_id.toStdString(), muz::native_path(core_path), false);
    QXlsx::Document core(core_path);
    REQUIRE(core.load());
    REQUIRE(core.sheetNames().contains("HAMDATA"));
    REQUIRE_FALSE(core.sheetNames().contains("MUHASEBE"));
    REQUIRE(core.selectSheet("HAMDATA"));
    REQUIRE(core.dimension().lastRow() == book.dimension().lastRow());
    REQUIRE(core.dimension().lastColumn() == book.dimension().lastColumn());
    REQUIRE(core.read(2, 7).toString() == book.read(2, 7).toString());
    REQUIRE(core.read(2, 10).toString() == book.read(2, 10).toString());
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

TEST_CASE("Web HAMDATA with accounting keeps missing identity in HAMDATA and lets core exclude it from MUHASEBE", "[integration][web][hamdata]") {
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
    cells["debtorId"] = "";
    row["cells"] = cells;
    review = post_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/batches/%2/review/save")
        .arg(server.port()).arg(batch_id)), {{"rows", QJsonArray{row}}});
    REQUIRE(first_row(review)["status"].toString() == "review");
    REQUIRE_FALSE(first_row(review)["approved"].toBool());

    const auto created = post_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/batches/%2/exports/hamdata-with-accounting")
        .arg(server.port()).arg(batch_id)), {}, 201);
    const auto download = request(network, QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1%2")
        .arg(server.port()).arg(created["downloadUrl"].toString()))));
    REQUIRE(download.status == 200);

    const auto workbook = save_xlsx(download.body, temp, QStringLiteral("missing-id.xlsx"));
    QXlsx::Document book(workbook);
    REQUIRE(book.load());
    REQUIRE(book.selectSheet("HAMDATA"));
    REQUIRE(book.dimension().lastRow() == 2);
    REQUIRE(book.read(2, 10).toString().isEmpty());
    REQUIRE(book.selectSheet("MUHASEBE"));
    REQUIRE(book.read(2, 1).toString().isEmpty());
    REQUIRE(book.read(2, 2).toString().isEmpty());

    const auto core_path = temp.path() + "/core-missing-id.xlsx";
    workspace.export_hamdata(batch_id.toStdString(), muz::native_path(core_path), true);
    QXlsx::Document core(core_path);
    REQUIRE(core.load());
    REQUIRE(core.selectSheet("HAMDATA"));
    REQUIRE(core.read(2, 10).toString() == book.read(2, 10).toString());
    REQUIRE(core.selectSheet("MUHASEBE"));
    REQUIRE(core.read(2, 1).toString() == book.read(2, 1).toString());
}

TEST_CASE("Web HAMDATA export reports unknown ids without exposing paths", "[integration][web][hamdata]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    muz::LocalWorkspace workspace(std::filesystem::path(temp.path().toStdString()) / "workspace");
    muz::WebServer server(workspace);
    REQUIRE(server.listen(QHostAddress::LocalHost, 0));

    QNetworkAccessManager network;
    const auto unknown_batch = post_json(network,
        QUrl(QStringLiteral("http://127.0.0.1:%1/api/batches/00000000-0000-0000-0000-000000000000/exports/hamdata")
        .arg(server.port())), {}, 404);
    REQUIRE(unknown_batch["error"].toObject()["code"].toString() == "batch_not_found");
    REQUIRE_FALSE(QJsonDocument(unknown_batch).toJson(QJsonDocument::Compact).contains(temp.path().toUtf8()));

    const auto unknown_export = get_json(network,
        QUrl(QStringLiteral("http://127.0.0.1:%1/api/exports/00000000-0000-0000-0000-000000000000/download")
        .arg(server.port())), 404);
    REQUIRE(unknown_export["error"].toObject()["code"].toString() == "export_not_found");
    REQUIRE_FALSE(QJsonDocument(unknown_export).toJson(QJsonDocument::Compact).contains(temp.path().toUtf8()));
}

TEST_CASE("Web accounting return upload renders persisted VAR YOK and fanout state from core", "[integration][web][return]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    muz::LocalWorkspace workspace(std::filesystem::path(temp.path().toStdString()) / "workspace");
    muz::WebServer server(workspace);
    REQUIRE(server.listen(QHostAddress::LocalHost, 0));

    const auto workbook = returned_hamdata_workbook(temp, [](QXlsx::Document& book) {
        REQUIRE(book.selectSheet("MUHASEBE"));
        REQUIRE(book.write(2, 4, 123.45));
        REQUIRE(book.write(4, 4, 0));
        REQUIRE(book.write(5, 4, -25.5));
    });
    const auto core = muz::read_accounting_return(workbook);
    REQUIRE(core.rows.size() == 5);

    QNetworkAccessManager network;
    auto payload = upload_return(network, server.port(), workbook);
    const auto return_id = payload["return"].toObject()["id"].toString();
    REQUIRE(return_id.size() == 36);
    REQUIRE(payload["summary"].toObject()["total"].toInt() == 5);
    REQUIRE(payload["summary"].toObject()["var"].toInt() == 4);
    REQUIRE(payload["summary"].toObject()["yok"].toInt() == 1);
    REQUIRE(payload["summary"].toObject()["blocked"].toInt() == 0);
    REQUIRE_FALSE(QJsonDocument(payload).toJson(QJsonDocument::Compact).contains(temp.path().toUtf8()));

    const auto rows = payload["rows"].toArray();
    REQUIRE(rows[0].toObject()["decision"].toString() == "var");
    REQUIRE(rows[0].toObject()["amount"].toObject()["cents"].toString() == "12345");
    REQUIRE(rows[1].toObject()["decision"].toString() == "var");
    REQUIRE(rows[1].toObject()["amount"].toObject()["cents"].toString() == "12345");
    REQUIRE(rows[1].toObject()["caseNumber"].toString() == "2026/2");
    REQUIRE(rows[2].toObject()["decision"].toString() == "yok");
    REQUIRE(rows[2].toObject()["debtor"].toString() == "AYNI AD");
    REQUIRE(rows[2].toObject()["debtorId"].toString() == "22222222222");
    REQUIRE(rows[3].toObject()["decision"].toString() == "var");
    REQUIRE(rows[3].toObject()["amount"].toObject()["cents"].toString() == "0");
    REQUIRE(rows[4].toObject()["decision"].toString() == "var");
    REQUIRE(rows[4].toObject()["amount"].toObject()["cents"].toString() == "-2550");

    payload = get_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/accounting-returns/%2")
        .arg(server.port()).arg(return_id)));
    REQUIRE(payload["summary"].toObject()["total"].toInt() == 5);
    REQUIRE(payload["rows"].toArray()[0].toObject()["amount"].toObject()["cents"].toString() ==
        QString::number(*core.rows[0].available_cents));
}

TEST_CASE("Web accounting return upload surfaces missing duplicate and invalid-cell blockers from core", "[integration][web][return]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    muz::LocalWorkspace workspace(std::filesystem::path(temp.path().toStdString()) / "workspace");
    muz::WebServer server(workspace);
    REQUIRE(server.listen(QHostAddress::LocalHost, 0));
    QNetworkAccessManager network;

    const auto missing = returned_hamdata_workbook(temp, [](QXlsx::Document& book) {
        REQUIRE(book.selectSheet("MUHASEBE"));
        REQUIRE(book.write(2, 1, "99999999999"));
    });
    auto payload = upload_return(network, server.port(), missing);
    REQUIRE(payload["summary"].toObject()["blocked"].toInt() == 2);
    REQUIRE(payload["summary"].toObject()["var"].toInt() == 0);
    REQUIRE(payload["summary"].toObject()["yok"].toInt() == 3);
    REQUIRE(payload["rows"].toArray()[0].toObject()["decision"].toString() == "blocked");
    REQUIRE(payload["rows"].toArray()[1].toObject()["decision"].toString() == "blocked");
    REQUIRE(payload["rows"].toArray()[0].toObject()["blockers"].toArray().first().toString().contains("muhasebe"));
    payload = get_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/accounting-returns/%2")
        .arg(server.port()).arg(payload["return"].toObject()["id"].toString())));
    REQUIRE(payload["summary"].toObject()["blocked"].toInt() == 2);
    REQUIRE(payload["summary"].toObject()["var"].toInt() == 0);
    REQUIRE(payload["summary"].toObject()["yok"].toInt() == 3);
    REQUIRE(payload["rows"].toArray()[0].toObject()["decision"].toString() == "blocked");

    const auto duplicate = returned_hamdata_workbook(temp, [](QXlsx::Document& book) {
        REQUIRE(book.selectSheet("MUHASEBE"));
        REQUIRE(book.write(6, 1, "11111111111"));
        REQUIRE(book.write(6, 2, "DUPLICATE"));
        REQUIRE(book.write(6, 4, 50.0));
    });
    payload = upload_return(network, server.port(), duplicate);
    REQUIRE(payload["summary"].toObject()["blocked"].toInt() == 2);
    REQUIRE(payload["summary"].toObject()["var"].toInt() == 0);
    REQUIRE(payload["summary"].toObject()["yok"].toInt() == 3);
    REQUIRE(payload["rows"].toArray()[0].toObject()["decision"].toString() == "blocked");
    REQUIRE(payload["rows"].toArray()[1].toObject()["decision"].toString() == "blocked");
    REQUIRE(QJsonDocument(payload).toJson(QJsonDocument::Compact).contains("yinelenen"));

    const auto invalid = returned_hamdata_workbook(temp, [](QXlsx::Document& book) {
        REQUIRE(book.selectSheet("MUHASEBE"));
        REQUIRE(book.currentWorksheet()->writeFormula(2, 4, QXlsx::CellFormula("1+1")));
    });
    payload = upload_return(network, server.port(), invalid);
    REQUIRE(payload["summary"].toObject()["blocked"].toInt() == 2);
    REQUIRE(payload["summary"].toObject()["var"].toInt() == 0);
    REQUIRE(payload["summary"].toObject()["yok"].toInt() == 3);
    REQUIRE(payload["rows"].toArray()[0].toObject()["decision"].toString() == "blocked");
    REQUIRE(payload["rows"].toArray()[1].toObject()["decision"].toString() == "blocked");
    REQUIRE_FALSE(payload["rows"].toArray()[0].toObject()["blockers"].toArray().isEmpty());
    REQUIRE_FALSE(QJsonDocument(payload).toJson(QJsonDocument::Compact).contains(temp.path().toUtf8()));
}

TEST_CASE("Web accounting return API rejects malformed uploads and unknown return ids", "[integration][web][return]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    muz::LocalWorkspace workspace(std::filesystem::path(temp.path().toStdString()) / "workspace");
    muz::WebServer server(workspace);
    REQUIRE(server.listen(QHostAddress::LocalHost, 0));

    QNetworkAccessManager network;
    QNetworkRequest plain(QUrl(QStringLiteral("http://127.0.0.1:%1/api/accounting-returns").arg(server.port())));
    plain.setHeader(QNetworkRequest::ContentTypeHeader, "text/plain");
    auto result = request(network, plain, "not an xlsx");
    REQUIRE(result.status == 415);
    auto payload = QJsonDocument::fromJson(result.body).object();
    REQUIRE(payload["error"].toObject()["code"].toString() == "unsupported_media_type");
    REQUIRE_FALSE(result.body.contains(temp.path().toUtf8()));

    payload = upload_return(network, server.port(), "bad xlsx", 400);
    REQUIRE(payload["error"].toObject()["code"].toString() == "return_import_failed");
    REQUIRE_FALSE(QJsonDocument(payload).toJson(QJsonDocument::Compact).contains(temp.path().toUtf8()));

    payload = upload_return(network, server.port(), "bad xlsx", 400, "wrong");
    REQUIRE(payload["error"].toObject()["code"].toString() == "missing_file");

    payload = get_json(network,
        QUrl(QStringLiteral("http://127.0.0.1:%1/api/accounting-returns/00000000-0000-0000-0000-000000000000")
        .arg(server.port())), 404);
    REQUIRE(payload["error"].toObject()["code"].toString() == "return_not_found");
}


TEST_CASE("Web response profile preview export and PDF downloads use C++ workspace behavior", "[integration][web][response]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    muz::LocalWorkspace workspace(std::filesystem::path(temp.path().toStdString()) / "workspace");
    muz::WebServer server(workspace);
    REQUIRE(server.listen(QHostAddress::LocalHost, 0));

    const auto workbook = returned_hamdata_workbook(temp, [](QXlsx::Document& book) {
        REQUIRE(book.selectSheet("MUHASEBE"));
        REQUIRE(book.write(2, 4, 123.45));
        REQUIRE(book.write(4, 4, 0));
        REQUIRE(book.write(5, 4, -25.5));
    });

    QNetworkAccessManager network;
    auto payload = upload_return(network, server.port(), workbook);
    const auto return_id = payload["return"].toObject()["id"].toString();
    const auto rows = payload["rows"].toArray();
    REQUIRE(rows.size() == 5);
    QJsonArray row_ids;
    for (const auto& row : rows) row_ids.append(row.toObject()["id"].toString());

    const QJsonObject saved_profile{{"lawyer", "Av. SAVED"}, {"address", "SAVED ADRES"}};
    const QJsonObject preview_profile{{"lawyer", "Av. UNSAVED PREVIEW"}, {"address", "UNSAVED PREVIEW ADRES"}};
    const QJsonObject generated_profile{{"lawyer", "Av. WEB TEST"}, {"address", "WEB TEST ADRES"}};
    auto profile_payload = post_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/response-profile").arg(server.port())),
        {{"profile", saved_profile}});
    REQUIRE(profile_payload["profile"].toObject()["lawyer"].toString() == "Av. SAVED");

    const auto first_row_id = rows[0].toObject()["id"].toString();
    auto preview = post_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/accounting-returns/%2/responses/%3/preview")
        .arg(server.port()).arg(return_id, first_row_id)), {{"profile", preview_profile}});
    REQUIRE(preview["html"].toString().contains("Av. UNSAVED PREVIEW"));
    REQUIRE(preview["html"].toString().contains("UNSAVED PREVIEW ADRES"));
    REQUIRE_FALSE(preview["html"].toString().contains("Av. SAVED"));
    REQUIRE(preview["html"].toString().contains("TEST MUHATAP"));
    profile_payload = get_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/response-profile").arg(server.port())));
    REQUIRE(profile_payload["profile"].toObject()["lawyer"].toString() == "Av. SAVED");
    REQUIRE(profile_payload["profile"].toObject()["address"].toString() == "SAVED ADRES");

    const QJsonObject changed_preview_profile{{"lawyer", "Av. UNSAVED CHANGED"}, {"address", "CHANGED PREVIEW ADRES"}};
    preview = post_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/accounting-returns/%2/responses/%3/preview")
        .arg(server.port()).arg(return_id, first_row_id)), {{"profile", changed_preview_profile}});
    REQUIRE(preview["html"].toString().contains("Av. UNSAVED CHANGED"));
    REQUIRE_FALSE(preview["html"].toString().contains("Av. UNSAVED PREVIEW"));
    profile_payload = get_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/response-profile").arg(server.port())));
    REQUIRE(profile_payload["profile"].toObject()["lawyer"].toString() == "Av. SAVED");

    const auto old_get_preview = get_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/accounting-returns/%2/responses/%3/preview")
        .arg(server.port()).arg(return_id, first_row_id)), 405);
    REQUIRE(old_get_preview["error"].toObject()["code"].toString() == "method_not_allowed");

    auto created = post_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/accounting-returns/%2/response-exports")
        .arg(server.port()).arg(return_id)), {{"rowIds", row_ids}, {"profile", generated_profile}}, 201);
    REQUIRE(created["exportId"].toString().size() == 36);
    REQUIRE(created["returnId"].toString() == return_id);
    REQUIRE_FALSE(QJsonDocument(created).toJson(QJsonDocument::Compact).contains(temp.path().toUtf8()));
    const auto files = created["files"].toArray();
    REQUIRE(files.size() == 5);
    int var_count = 0;
    int yok_count = 0;
    for (const auto& value : files) {
        const auto file = value.toObject();
        REQUIRE(file["id"].toString().size() == 36);
        REQUIRE(file["downloadUrl"].toString().startsWith("/api/response-exports/"));
        REQUIRE_FALSE(file["downloadUrl"].toString().contains(temp.path()));
        if (file["filename"].toString().endsWith("_VAR.pdf")) ++var_count;
        if (file["filename"].toString().endsWith("_YOK.pdf")) ++yok_count;
    }
    REQUIRE(var_count == 4);
    REQUIRE(yok_count == 1);

    const auto export_id = created["exportId"].toString();
    const auto state = get_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/response-exports/%2")
        .arg(server.port()).arg(export_id)));
    REQUIRE(state["files"].toArray().size() == files.size());

    const auto first_file = files[0].toObject();
    const auto download = request(network, QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1%2")
        .arg(server.port()).arg(first_file["downloadUrl"].toString()))));
    REQUIRE(download.status == 200);
    REQUIRE(download.content_type == "application/pdf");
    REQUIRE(download.content_disposition.contains("attachment"));
    REQUIRE(download.content_disposition.contains(first_file["filename"].toString().toUtf8()));
    REQUIRE(download.body.startsWith("%PDF"));
    const auto text = muz::extract_pdf(download.body);
    REQUIRE(text.error.empty());
    REQUIRE(text.text.contains("TEST MUHATAP"));
    REQUIRE(text.text.contains("2026/"));
}



TEST_CASE("Web response PDFs are stored in durable workspace generated storage", "[integration][web][response]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    const auto workspace_root = temp.path() + "/workspace";
    muz::LocalWorkspace workspace(muz::native_path(workspace_root));

    QString return_id;
    QString persisted_pdf;
    QString download_url;
    const QJsonObject profile{{"lawyer", "Av. WEB TEST"}, {"address", "WEB TEST ADRES"}};

    {
        muz::WebServer server(workspace);
        REQUIRE(server.listen(QHostAddress::LocalHost, 0));
        const auto workbook = returned_hamdata_workbook(temp, [](QXlsx::Document& book) {
            REQUIRE(book.selectSheet("MUHASEBE"));
            REQUIRE(book.write(2, 4, 123.45));
        });
        QNetworkAccessManager network;
        auto payload = upload_return(network, server.port(), workbook);
        return_id = payload["return"].toObject()["id"].toString();
        QJsonArray row_ids;
        row_ids.append(payload["rows"].toArray()[0].toObject()["id"].toString());

        const auto created = post_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/accounting-returns/%2/response-exports")
            .arg(server.port()).arg(return_id)), {{"rowIds", row_ids}, {"profile", profile}}, 201);
        REQUIRE_FALSE(QJsonDocument(created).toJson(QJsonDocument::Compact).contains(workspace_root.toUtf8()));
        const auto file = created["files"].toArray()[0].toObject();
        download_url = file["downloadUrl"].toString();
        REQUIRE_FALSE(download_url.contains(workspace_root));

        const auto download = request(network, QNetworkRequest(QUrl(QStringLiteral("http://127.0.0.1:%1%2")
            .arg(server.port()).arg(download_url))));
        REQUIRE(download.status == 200);
        REQUIRE(download.content_type == "application/pdf");
        REQUIRE_FALSE(download.content_disposition.contains(workspace_root.toUtf8()));

        const auto rows = workspace.response_rows(return_id.toStdString());
        REQUIRE_FALSE(rows.empty());
        persisted_pdf = QString::fromStdString(rows[0].output_pdf);
        REQUIRE_FALSE(persisted_pdf.isEmpty());
        const QFileInfo pdf_info(persisted_pdf);
        REQUIRE(pdf_info.exists());
        const auto durable_root = QFileInfo(workspace_root + "/generated/responses").canonicalFilePath();
        REQUIRE_FALSE(durable_root.isEmpty());
        const auto canonical_pdf = pdf_info.canonicalFilePath();
        REQUIRE(canonical_pdf.startsWith(durable_root + '/', Qt::CaseInsensitive));
    }

    REQUIRE(QFileInfo::exists(persisted_pdf));
    const auto persisted_rows = workspace.response_rows(return_id.toStdString());
    REQUIRE_FALSE(persisted_rows.empty());
    REQUIRE(QString::fromStdString(persisted_rows[0].output_pdf) == persisted_pdf);
    REQUIRE(QFileInfo::exists(QString::fromStdString(persisted_rows[0].output_pdf)));

    {
        muz::WebServer server(workspace);
        REQUIRE(server.listen(QHostAddress::LocalHost, 0));
        QNetworkAccessManager network;
        const auto reloaded = get_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/accounting-returns/%2")
            .arg(server.port()).arg(return_id)));
        REQUIRE(reloaded["rows"].toArray().size() > 0);
        REQUIRE_FALSE(QJsonDocument(reloaded).toJson(QJsonDocument::Compact).contains(workspace_root.toUtf8()));
    }

    REQUIRE(QFileInfo::exists(persisted_pdf));
}

TEST_CASE("Web response export rejects blocked rows and malformed identifiers without leaking paths", "[integration][web][response]") {
    QTemporaryDir temp;
    REQUIRE(temp.isValid());
    muz::LocalWorkspace workspace(std::filesystem::path(temp.path().toStdString()) / "workspace");
    muz::WebServer server(workspace);
    REQUIRE(server.listen(QHostAddress::LocalHost, 0));
    QNetworkAccessManager network;

    const QJsonObject profile{{"lawyer", "Av. WEB TEST"}, {"address", "WEB TEST ADRES"}};
    const auto workbook = returned_hamdata_workbook(temp, [](QXlsx::Document& book) {
        REQUIRE(book.selectSheet("MUHASEBE"));
        REQUIRE(book.write(2, 4, 123.45));
    });
    auto payload = upload_return(network, server.port(), workbook);
    const auto return_id = payload["return"].toObject()["id"].toString();
    const auto valid_id = payload["rows"].toArray()[0].toObject()["id"].toString();

    auto failed = post_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/accounting-returns/%2/response-exports")
        .arg(server.port()).arg(return_id)), {{"rowIds", QJsonArray{}}, {"profile", profile}}, 400);
    REQUIRE(failed["error"].toObject()["code"].toString() == "bad_payload");

    failed = post_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/accounting-returns/%2/response-exports")
        .arg(server.port()).arg(return_id)), {{"rowIds", QJsonArray{valid_id, valid_id}}, {"profile", profile}}, 400);
    REQUIRE(failed["error"].toObject()["code"].toString() == "bad_row_id");

    failed = post_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/accounting-returns/%2/response-exports")
        .arg(server.port()).arg(return_id)), {{"rowIds", QJsonArray{"missing-row"}}, {"profile", profile}}, 404);
    REQUIRE(failed["error"].toObject()["code"].toString() == "row_not_found");

    failed = post_json(network,
        QUrl(QStringLiteral("http://127.0.0.1:%1/api/accounting-returns/00000000-0000-0000-0000-000000000000/response-exports")
        .arg(server.port())), {{"rowIds", QJsonArray{valid_id}}, {"profile", profile}}, 404);
    REQUIRE(failed["error"].toObject()["code"].toString() == "return_not_found");

    failed = post_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/accounting-returns/%2/response-exports")
        .arg(server.port()).arg(return_id)), {{"rowIds", QJsonArray{valid_id}}, {"profile", QJsonObject{{"lawyer", ""}, {"address", ""}}}}, 400);
    REQUIRE(failed["error"].toObject()["code"].toString() == "response_generation_failed");

    const auto blocked_workbook = returned_hamdata_workbook(temp, [](QXlsx::Document& book) {
        REQUIRE(book.selectSheet("MUHASEBE"));
        REQUIRE(book.write(2, 1, "99999999999"));
    });
    payload = upload_return(network, server.port(), blocked_workbook);
    const auto blocked_return_id = payload["return"].toObject()["id"].toString();
    const auto blocked_id = payload["rows"].toArray()[0].toObject()["id"].toString();
    REQUIRE(payload["rows"].toArray()[0].toObject()["status"].toString() == "blocked");
    failed = post_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/accounting-returns/%2/response-exports")
        .arg(server.port()).arg(blocked_return_id)), {{"rowIds", QJsonArray{blocked_id}}, {"profile", profile}}, 400);
    REQUIRE(failed["error"].toObject()["code"].toString() == "response_generation_failed");
    auto preview = post_json(network, QUrl(QStringLiteral("http://127.0.0.1:%1/api/accounting-returns/%2/responses/%3/preview")
        .arg(server.port()).arg(blocked_return_id, blocked_id)), {{"profile", profile}}, 400);
    REQUIRE(preview["error"].toObject()["code"].toString() == "preview_failed");
    REQUIRE_FALSE(QJsonDocument(failed).toJson(QJsonDocument::Compact).contains(temp.path().toUtf8()));

    auto unknown = get_json(network,
        QUrl(QStringLiteral("http://127.0.0.1:%1/api/response-exports/00000000-0000-0000-0000-000000000000")
        .arg(server.port())), 404);
    REQUIRE(unknown["error"].toObject()["code"].toString() == "response_export_not_found");

    unknown = get_json(network,
        QUrl(QStringLiteral("http://127.0.0.1:%1/api/response-exports/00000000-0000-0000-0000-000000000000/files/00000000-0000-0000-0000-000000000000")
        .arg(server.port())), 404);
    REQUIRE(unknown["error"].toObject()["code"].toString() == "response_export_not_found");
}
