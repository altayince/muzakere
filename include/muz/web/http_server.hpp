#pragma once

#include "muz/application/ports.hpp"
#include <QByteArray>
#include <QHostAddress>
#include <QObject>
#include <QString>
#include <filesystem>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

class QTemporaryDir;
class QTcpServer;
class QTcpSocket;

namespace muz {

class WebServer final : public QObject {
public:
    explicit WebServer(Workspace& workspace, QObject* parent = nullptr);
    ~WebServer() override;

    bool listen(const QHostAddress& address, quint16 port);
    void close();
    [[nodiscard]] quint16 port() const;

private:
    struct Request;
    struct Header {
        QByteArray name;
        QByteArray value;
    };
    struct ExportFile {
        std::filesystem::path path;
        QString filename;
        QByteArray content_type;
    };
    struct ResponseExportFile {
        std::string id;
        std::filesystem::path path;
        QString filename;
        QByteArray content_type;
    };
    struct ResponseExportSet {
        std::string return_id;
        std::vector<ResponseExportFile> files;
    };
    void accept_pending();
    void read_socket(QTcpSocket* socket, const std::shared_ptr<QByteArray>& buffer);
    void handle(QTcpSocket* socket, const Request& request);
    void respond(QTcpSocket* socket, int status, QByteArray body, QByteArray content_type = "application/json",
        const std::vector<Header>& headers = {}) const;
    [[nodiscard]] QByteArray static_asset(const QString& path, QByteArray& content_type) const;
    [[nodiscard]] QByteArray import_zip(const Request& request, int& status) const;
    [[nodiscard]] QByteArray review_state(const QString& batch_id, int& status) const;
    [[nodiscard]] QByteArray save_review(const QString& batch_id, const Request& request, int& status) const;
    [[nodiscard]] QByteArray approve_review(const QString& batch_id, const Request& request, int& status) const;
    [[nodiscard]] QByteArray create_hamdata_export(const QString& batch_id, bool with_accounting, int& status) const;
    [[nodiscard]] QByteArray download_export(const QString& export_id, int& status, QByteArray& content_type,
        std::vector<Header>& headers) const;
    [[nodiscard]] QByteArray import_accounting_return(const Request& request, int& status) const;
    [[nodiscard]] QByteArray accounting_return_state(const QString& return_id, int& status) const;
    [[nodiscard]] QByteArray response_profile_state(int& status) const;
    [[nodiscard]] QByteArray save_response_profile(const Request& request, int& status) const;
    [[nodiscard]] QByteArray preview_response(const QString& return_id, const QString& row_id, int& status) const;
    [[nodiscard]] QByteArray create_response_export(const QString& return_id, const Request& request, int& status) const;
    [[nodiscard]] QByteArray response_export_state(const QString& export_id, int& status) const;
    [[nodiscard]] QByteArray download_response_file(const QString& export_id, const QString& file_id, int& status,
        QByteArray& content_type, std::vector<Header>& headers) const;

    Workspace& workspace_;
    std::unique_ptr<QTcpServer> server_;
    std::unique_ptr<QTemporaryDir> export_dir_;
    mutable std::mutex export_mutex_;
    mutable std::map<std::string, ExportFile> exports_;
    mutable std::map<std::string, ResponseExportSet> response_exports_;
};

} // namespace muz
