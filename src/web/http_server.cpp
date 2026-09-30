#include "muz/web/http_server.hpp"
#include "muz/domain/accounting.hpp"
#include "qt_paths.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QUrl>
#include <QUuid>
#include <algorithm>
#include <optional>
#include <set>

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

QJsonObject review_json(const ProcessingBatch& batch, const std::vector<AccountingRow>& rows) {
    QJsonArray row_items;
    int ready = 0, review = 0, blocked = 0, approved = 0;
    for (const auto& row : rows) {
        const auto state = review_status(row);
        if (state == ReviewStatus::ready) ++ready;
        else if (state == ReviewStatus::review) ++review;
        else ++blocked;
        if (row.approved) ++approved;
        row_items.append(row_json(row));
    }
    return {
        {"batch", QJsonObject{
            {"id", text(batch.id)},
            {"status", text(batch.status)},
            {"importedCount", static_cast<int>(batch.imported_count)},
            {"duplicateCount", static_cast<int>(batch.duplicate_count)},
            {"issueCount", static_cast<int>(batch.issue_count)}
        }},
        {"summary", QJsonObject{
            {"ready", ready},
            {"review", review},
            {"blocked", blocked},
            {"approved", approved},
            {"total", row_items.size()}
        }},
        {"rows", row_items}
    };
}

std::optional<QJsonObject> parse_json_object(const QByteArray& body) {
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(body, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) return {};
    return document.object();
}

bool cells_from_json(const QJsonObject& object, std::array<std::string, column_count>& cells) {
    const std::array<const char*, column_count> keys{"serviceDate", "office", "caseNumber", "amount", "debtor",
        "debtorId", "creditor", "iban", "firstNotice", "notes"};
    const auto values = object.value("cells");
    if (!values.isObject()) return false;
    const auto cell_object = values.toObject();
    for (std::size_t index = 0; index < keys.size(); ++index) {
        const auto value = cell_object.value(keys[index]);
        if (!value.isString()) return false;
        cells[index] = value.toString().toStdString();
    }
    return true;
}

QStringList path_segments(const QString& path) {
    return path.split('/', Qt::SkipEmptyParts);
}
} // namespace

struct WebServer::Request {
    QByteArray method;
    QString path;
    QMap<QByteArray, QByteArray> headers;
    QByteArray body;
};

WebServer::WebServer(Workspace& workspace, QObject* parent)
    : QObject(parent), workspace_(workspace), server_(std::make_unique<QTcpServer>()),
      export_dir_(std::make_unique<QTemporaryDir>()) {
    if (!export_dir_->isValid()) throw Error(ErrorCode::file_io);
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
    const auto segments = path_segments(request.path);
    if (segments.size() == 3 && segments[0] == "api" && segments[1] == "batches" && segments[2].size() == 36 &&
        request.method == "GET") {
        int status = 200;
        respond(socket, status, review_state(segments[2], status));
        return;
    }
    if (segments.size() == 4 && segments[0] == "api" && segments[1] == "batches" && segments[3] == "review") {
        int status = 200;
        if (request.method == "GET") respond(socket, status, review_state(segments[2], status));
        else respond(socket, 405, json(error_body("method_not_allowed", "Method is not supported.")));
        return;
    }
    if (segments.size() == 5 && segments[0] == "api" && segments[1] == "batches" && segments[3] == "review" &&
        segments[4] == "save" && request.method == "POST") {
        int status = 200;
        respond(socket, status, save_review(segments[2], request, status));
        return;
    }
    if (segments.size() == 5 && segments[0] == "api" && segments[1] == "batches" && segments[3] == "review" &&
        segments[4] == "approve" && request.method == "POST") {
        int status = 200;
        respond(socket, status, approve_review(segments[2], request, status));
        return;
    }
    if (segments.size() == 5 && segments[0] == "api" && segments[1] == "batches" && segments[3] == "exports" &&
        (segments[4] == "hamdata" || segments[4] == "hamdata-with-accounting") && request.method == "POST") {
        int status = 200;
        respond(socket, status, create_hamdata_export(segments[2], segments[4] == "hamdata-with-accounting", status));
        return;
    }
    if (segments.size() == 4 && segments[0] == "api" && segments[1] == "exports" && segments[3] == "download" &&
        request.method == "GET") {
        int status = 200;
        QByteArray content_type;
        std::vector<Header> headers;
        const auto body = download_export(segments[2], status, content_type, headers);
        respond(socket, status, body, content_type.isEmpty() ? QByteArray("application/json") : content_type, headers);
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

void WebServer::respond(QTcpSocket* socket, int status, QByteArray body, QByteArray content_type,
        const std::vector<Header>& headers) const {
    QByteArray response = "HTTP/1.1 " + QByteArray::number(status) + ' ' + phrase(status) + "\r\n";
    response += "Content-Type: " + content_type + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Connection: close\r\n";
    response += "X-Content-Type-Options: nosniff\r\n";
    for (const auto& header : headers) response += header.name + ": " + header.value + "\r\n";
    response += "\r\n";
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
        QJsonArray issues;
        for (const auto& issue : imported.issues)
            issues.append(QJsonObject{{"code", text(issue.code)}, {"source", text(issue.source_path)}});
        auto body = review_json(imported.batch, rows);
        body.insert("issues", issues);
        auto batch = body["batch"].toObject();
        batch.insert("archiveName", text(imported.archive_name));
        body["batch"] = batch;
        status = 201;
        return json(body);
    } catch (const Error& error) {
        status = error.code() == ErrorCode::invalid_input ? 400 : 500;
        return json(error_body("import_failed", "ZIP could not be imported or reviewed."));
    }
}

QByteArray WebServer::review_state(const QString& batch_id, int& status) const {
    try {
        const auto id = batch_id.toStdString();
        const auto batches = workspace_.batches();
        const auto batch = std::find_if(batches.begin(), batches.end(), [&](const auto& item) { return item.id == id; });
        if (batch == batches.end()) {
            status = 404;
            return json(error_body("batch_not_found", "Batch was not found."));
        }
        return json(review_json(*batch, workspace_.accounting_rows(id)));
    } catch (const Error&) {
        status = 500;
        return json(error_body("review_failed", "Review state could not be loaded."));
    }
}

QByteArray WebServer::save_review(const QString& batch_id, const Request& request, int& status) const {
    try {
        const auto payload = parse_json_object(request.body);
        if (!payload || !payload->value("rows").isArray()) {
            status = 400;
            return json(error_body("bad_payload", "Rows array is required."));
        }
        const auto id = batch_id.toStdString();
        const auto current = workspace_.accounting_rows(id);
        std::vector<AccountingRow> edits;
        std::set<std::string> seen;
        for (const auto& value : payload->value("rows").toArray()) {
            if (!value.isObject()) {
                status = 400;
                return json(error_body("bad_payload", "Each row edit must be an object."));
            }
            const auto object = value.toObject();
            const auto row_id = object.value("id").toString().toStdString();
            if (row_id.empty() || !seen.insert(row_id).second) {
                status = 400;
                return json(error_body("bad_row_id", "Row id is missing or duplicated."));
            }
            const auto found = std::find_if(current.begin(), current.end(), [&](const auto& row) { return row.id == row_id; });
            if (found == current.end()) {
                status = 404;
                return json(error_body("row_not_found", "Review row was not found."));
            }
            auto edit = *found;
            if (!cells_from_json(object, edit.cells)) {
                status = 400;
                return json(error_body("bad_payload", "Editable cells are incomplete."));
            }
            edit.approved = false;
            edits.push_back(std::move(edit));
        }
        if (edits.empty()) {
            status = 400;
            return json(error_body("bad_payload", "At least one row is required."));
        }
        workspace_.review_accounting(id, edits);
        return review_state(batch_id, status);
    } catch (const Error& error) {
        status = error.code() == ErrorCode::invalid_input ? 400 : 500;
        return json(error_body("save_failed", "Rows could not be saved."));
    }
}

QByteArray WebServer::approve_review(const QString& batch_id, const Request& request, int& status) const {
    try {
        const auto payload = parse_json_object(request.body);
        if (!payload || !payload->value("rowIds").isArray()) {
            status = 400;
            return json(error_body("bad_payload", "rowIds array is required."));
        }
        const auto id = batch_id.toStdString();
        const auto current = workspace_.accounting_rows(id);
        std::vector<AccountingRow> approvals;
        std::set<std::string> seen;
        for (const auto& value : payload->value("rowIds").toArray()) {
            if (!value.isString()) {
                status = 400;
                return json(error_body("bad_row_id", "Row id must be a string."));
            }
            const auto row_id = value.toString().toStdString();
            if (row_id.empty() || !seen.insert(row_id).second) {
                status = 400;
                return json(error_body("bad_row_id", "Row id is missing or duplicated."));
            }
            const auto found = std::find_if(current.begin(), current.end(), [&](const auto& row) { return row.id == row_id; });
            if (found == current.end()) {
                status = 404;
                return json(error_body("row_not_found", "Review row was not found."));
            }
            auto approval = *found;
            approval.approved = true;
            approvals.push_back(std::move(approval));
        }
        if (approvals.empty()) {
            status = 400;
            return json(error_body("bad_payload", "At least one row id is required."));
        }
        workspace_.review_accounting(id, approvals);
        return review_state(batch_id, status);
    } catch (const Error& error) {
        status = error.code() == ErrorCode::invalid_input ? 400 : 500;
        return json(error_body("approval_failed", "Rows could not be approved."));
    }
}

QByteArray WebServer::create_hamdata_export(const QString& batch_id, bool with_accounting, int& status) const {
    try {
        const auto id = batch_id.toStdString();
        const auto batches = workspace_.batches();
        const auto batch = std::find_if(batches.begin(), batches.end(), [&](const auto& item) { return item.id == id; });
        if (batch == batches.end()) {
            status = 404;
            return json(error_body("batch_not_found", "Batch was not found."));
        }
        const auto export_id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto prefix = with_accounting ? QStringLiteral("HAMDATA-MUHASEBE-") : QStringLiteral("HAMDATA-");
        const auto filename = prefix + QString::fromStdString(id).left(8) + QStringLiteral(".xlsx");
        const auto output = export_dir_->path() + '/' + export_id + QStringLiteral(".xlsx");
        workspace_.export_hamdata(id, native_path(output), with_accounting);

        {
            std::lock_guard guard(export_mutex_);
            exports_.insert({export_id.toStdString(), {native_path(output), filename,
                "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"}});
        }
        status = 201;
        return json({
            {"exportId", export_id},
            {"filename", filename},
            {"downloadUrl", QStringLiteral("/api/exports/%1/download").arg(export_id)}
        });
    } catch (const Error& error) {
        status = error.code() == ErrorCode::invalid_input ? 400 : 500;
        return json(error_body("export_failed", "HAMDATA workbook could not be generated."));
    }
}

QByteArray WebServer::download_export(const QString& export_id, int& status, QByteArray& content_type,
        std::vector<Header>& headers) const {
    ExportFile export_file;
    {
        std::lock_guard guard(export_mutex_);
        const auto found = exports_.find(export_id.toStdString());
        if (found == exports_.end()) {
            status = 404;
            content_type = "application/json";
            return json(error_body("export_not_found", "Export was not found."));
        }
        export_file = found->second;
    }

    QFile file(qpath(export_file.path));
    if (!file.open(QIODevice::ReadOnly)) {
        status = 500;
        content_type = "application/json";
        return json(error_body("download_failed", "Export file could not be read."));
    }
    status = 200;
    content_type = export_file.content_type;
    headers.push_back({"Content-Disposition", QByteArray("attachment; filename=\"") + export_file.filename.toUtf8() + "\""});
    headers.push_back({"Cache-Control", "no-store"});
    return file.readAll();
}

} // namespace muz
