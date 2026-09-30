#include "muz/web/http_server.hpp"
#include "muz/domain/accounting.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrl>
#include <optional>

extern int qInitResources_web();

namespace {
void init_web_resources() {
    qInitResources_web();
}
} // namespace

namespace muz {
namespace {
constexpr qsizetype max_header_bytes = 64 * 1024;
constexpr qsizetype max_request_bytes = 270 * 1024 * 1024;

struct HeaderBlock {
    QByteArray method;
    QByteArray target;
    QMap<QByteArray, QByteArray> headers;
    qsizetype content_length{};
};

QByteArray lower(QByteArray value) {
    for (auto& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return value;
}

QByteArray phrase(int status) {
    switch (status) {
    case 200: return "OK";
    case 201: return "Created";
    case 400: return "Bad Request";
    case 404: return "Not Found";
    case 405: return "Method Not Allowed";
    case 413: return "Payload Too Large";
    case 415: return "Unsupported Media Type";
    default: return "Internal Server Error";
    }
}

QJsonObject error_body(const char* code, const QString& message) {
    return {{"error", QJsonObject{{"code", code}, {"message", message}}}};
}

QByteArray json(const QJsonObject& object) {
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

QString text(const std::string& value) {
    return QString::fromStdString(value);
}

QString status_name(ReviewStatus status) {
    switch (status) {
    case ReviewStatus::ready: return "ready";
    case ReviewStatus::review: return "review";
    case ReviewStatus::blocked: return "blocked";
    }
    return "blocked";
}

QString status_color(ReviewStatus status) {
    switch (status) {
    case ReviewStatus::ready: return "green";
    case ReviewStatus::review: return "yellow";
    case ReviewStatus::blocked: return "red";
    }
    return "red";
}

QJsonArray strings(const std::vector<std::string>& values) {
    QJsonArray result;
    for (const auto& value : values) result.append(text(value));
    return result;
}

QJsonObject row_json(const AccountingRow& row) {
    const auto status = review_status(row);
    const QJsonObject cells{
        {"serviceDate", text(row.cells[service_date])},
        {"office", text(row.cells[office])},
        {"caseNumber", text(row.cells[case_number])},
        {"amount", text(row.cells[amount])},
        {"debtor", text(row.cells[debtor])},
        {"debtorId", text(row.cells[debtor_id])},
        {"creditor", text(row.cells[creditor])},
        {"iban", text(row.cells[iban])},
        {"firstNotice", text(row.cells[first_notice])},
        {"notes", text(row.cells[notes])},
        {"recipient", text(recipient_text(row))}
    };
    QJsonArray blockers;
    if (!can_approve(row)) blockers.append("approval_requirements_not_met");
    if (row.pair_conflict) blockers.append("pair_conflict");
    if (row.identity_conflict) blockers.append("identity_conflict");
    return {
        {"id", text(row.id)},
        {"documentId", text(row.document_id)},
        {"status", status_name(status)},
        {"color", status_color(status)},
        {"approved", row.approved},
        {"source", text(row.source_path)},
        {"matchConfidence", row.match_confidence},
        {"cells", cells},
        {"warnings", strings(row.warnings)},
        {"blockers", blockers}
    };
}

std::optional<HeaderBlock> parse_headers(const QByteArray& bytes, qsizetype header_end) {
    const auto lines = bytes.left(header_end).split('\n');
    if (lines.isEmpty()) return {};
    const auto request = lines.front().trimmed().split(' ');
    if (request.size() != 3) return {};
    HeaderBlock result;
    result.method = request[0];
    result.target = request[1];
    for (int index = 1; index < lines.size(); ++index) {
        const auto line = lines[index].trimmed();
        const auto colon = line.indexOf(':');
        if (colon <= 0) continue;
        result.headers.insert(lower(line.left(colon).trimmed()), line.mid(colon + 1).trimmed());
    }
    bool ok = true;
    result.content_length = result.headers.value("content-length", "0").toLongLong(&ok);
    if (!ok || result.content_length < 0) return {};
    return result;
}

std::optional<QByteArray> multipart_file(const QByteArray& body, const QByteArray& content_type) {
    const auto marker = QByteArray("boundary=");
    const auto boundary_at = content_type.indexOf(marker);
    if (boundary_at < 0) return {};
    auto boundary = content_type.mid(boundary_at + marker.size()).trimmed();
    if (boundary.startsWith('"') && boundary.endsWith('"') && boundary.size() >= 2)
        boundary = boundary.mid(1, boundary.size() - 2);
    if (boundary.isEmpty() || boundary.size() > 200) return {};
    const auto delimiter = "--" + boundary;
    qsizetype cursor = body.indexOf(delimiter);
    while (cursor >= 0) {
        cursor += delimiter.size();
        if (body.mid(cursor, 2) == "--") return {};
        if (body.mid(cursor, 2) == "\r\n") cursor += 2;
        const auto header_end = body.indexOf("\r\n\r\n", cursor);
        if (header_end < 0) return {};
        const auto headers = body.mid(cursor, header_end - cursor);
        const auto data_start = header_end + 4;
        const auto next = body.indexOf("\r\n" + delimiter, data_start);
        if (next < 0) return {};
        if (headers.contains("name=\"file\"") && headers.contains("filename="))
            return body.mid(data_start, next - data_start);
        cursor = body.indexOf(delimiter, next);
    }
    return {};
}
} // namespace

struct WebServer::Request {
    QByteArray method;
    QString path;
    QMap<QByteArray, QByteArray> headers;
    QByteArray body;
};

WebServer::WebServer(Workspace& workspace, QObject* parent)
    : QObject(parent), workspace_(workspace), server_(std::make_unique<QTcpServer>()) {
    init_web_resources();
    connect(server_.get(), &QTcpServer::newConnection, this, [this] { accept_pending(); });
}

WebServer::~WebServer() = default;

bool WebServer::listen(const QHostAddress& address, quint16 port) {
    return server_->listen(address, port);
}

void WebServer::close() {
    server_->close();
}

quint16 WebServer::port() const {
    return server_->serverPort();
}

void WebServer::accept_pending() {
    while (auto* socket = server_->nextPendingConnection()) {
        socket->setParent(this);
        auto buffer = std::make_shared<QByteArray>();
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer] { read_socket(socket, buffer); });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void WebServer::read_socket(QTcpSocket* socket, const std::shared_ptr<QByteArray>& buffer) {
    buffer->append(socket->readAll());
    const auto header_end = buffer->indexOf("\r\n\r\n");
    if (header_end < 0) {
        if (buffer->size() > max_header_bytes) respond(socket, 400, json(error_body("bad_request", "Header is too large.")));
        return;
    }
    const auto parsed = parse_headers(*buffer, header_end);
    if (!parsed) {
        respond(socket, 400, json(error_body("bad_request", "Request could not be parsed.")));
        return;
    }
    if (parsed->content_length > max_request_bytes) {
        respond(socket, 413, json(error_body("payload_too_large", "Upload is too large.")));
        return;
    }
    const auto full_size = header_end + 4 + parsed->content_length;
    if (buffer->size() < full_size) return;
    handle(socket, {parsed->method, QUrl::fromEncoded(parsed->target).path(), parsed->headers,
        buffer->mid(header_end + 4, parsed->content_length)});
}

void WebServer::handle(QTcpSocket* socket, const Request& request) {
    if (request.method == "GET" && request.path == "/health") {
        respond(socket, 200, json({{"status", "ok"}, {"service", "muzakere_server"}}));
        return;
    }
    if (request.method == "POST" && request.path == "/api/imports") {
        int status = 200;
        const auto body = import_zip(request, status);
        respond(socket, status, body);
        return;
    }
    if (request.method == "GET") {
        QByteArray content_type;
        const auto body = static_asset(request.path, content_type);
        if (!body.isEmpty()) respond(socket, 200, body, content_type);
        else respond(socket, 404, json(error_body("not_found", "Endpoint not found.")));
        return;
    }
    respond(socket, 405, json(error_body("method_not_allowed", "Method is not supported.")));
}

void WebServer::respond(QTcpSocket* socket, int status, QByteArray body, QByteArray content_type) const {
    QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + ' ' + phrase(status) + "\r\n";
    response += "Content-Type: " + content_type + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Connection: close\r\n";
    response += "X-Content-Type-Options: nosniff\r\n\r\n";
    response += body;
    socket->write(response);
    socket->disconnectFromHost();
}

QByteArray WebServer::static_asset(const QString& path, QByteArray& content_type) const {
    const auto resource = path == "/" ? QStringLiteral(":/web/index.html") : QStringLiteral(":/web") + path;
    if (resource.contains("..")) return {};
    QFile file(resource);
    if (!file.open(QIODevice::ReadOnly)) return {};
    if (resource.endsWith(".html")) content_type = "text/html; charset=utf-8";
    else if (resource.endsWith(".css")) content_type = "text/css; charset=utf-8";
    else if (resource.endsWith(".js")) content_type = "application/javascript; charset=utf-8";
    else content_type = "application/octet-stream";
    return file.readAll();
}

QByteArray WebServer::import_zip(const Request& request, int& status) const {
    try {
        const auto content_type = request.headers.value("content-type");
        const auto type = lower(content_type);
        std::optional<QByteArray> upload;
        if (type.startsWith("application/zip") || type.startsWith("application/octet-stream"))
            upload = request.body;
        else if (type.startsWith("multipart/form-data"))
            upload = multipart_file(request.body, content_type);
        else {
            status = 415;
            return json(error_body("unsupported_media_type", "ZIP upload expected."));
        }
        if (!upload || upload->isEmpty()) {
            status = 400;
            return json(error_body("missing_file", "Upload must contain a ZIP file field named file."));
        }
        QTemporaryDir temporary;
        if (!temporary.isValid()) throw Error(ErrorCode::file_io);
        const auto upload_path = temporary.path() + "/upload.zip";
        QFile file(upload_path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly) || file.write(*upload) != upload->size())
            throw Error(ErrorCode::file_io);
        file.close();
        const auto imported = workspace_.import_archive(std::filesystem::path(upload_path.toStdString()));
        const auto rows = workspace_.prepare_accounting(imported.batch.id);
        QJsonArray row_items;
        int ready = 0, review = 0, blocked = 0;
        for (const auto& row : rows) {
            const auto state = review_status(row);
            if (state == ReviewStatus::ready) ++ready;
            else if (state == ReviewStatus::review) ++review;
            else ++blocked;
            row_items.append(row_json(row));
        }
        QJsonArray issues;
        for (const auto& issue : imported.issues)
            issues.append(QJsonObject{{"code", text(issue.code)}, {"source", text(issue.source_path)}});
        status = 201;
        return json({
            {"batch", QJsonObject{
                {"id", text(imported.batch.id)},
                {"status", text(imported.batch.status)},
                {"importedCount", static_cast<int>(imported.batch.imported_count)},
                {"duplicateCount", static_cast<int>(imported.batch.duplicate_count)},
                {"issueCount", static_cast<int>(imported.batch.issue_count)},
                {"archiveName", text(imported.archive_name)}
            }},
            {"summary", QJsonObject{{"ready", ready}, {"review", review}, {"blocked", blocked}, {"total", row_items.size()}}},
            {"rows", row_items},
            {"issues", issues}
        });
    } catch (const Error& error) {
        status = error.code() == ErrorCode::invalid_input ? 400 : 500;
        return json(error_body("import_failed", "ZIP could not be imported or reviewed."));
    }
}

} // namespace muz
