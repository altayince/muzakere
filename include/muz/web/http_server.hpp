#pragma once

#include "muz/application/ports.hpp"
#include <QHostAddress>
#include <QObject>
#include <memory>

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
    void accept_pending();
    void read_socket(QTcpSocket* socket, const std::shared_ptr<QByteArray>& buffer);
    void handle(QTcpSocket* socket, const Request& request);
    void respond(QTcpSocket* socket, int status, QByteArray body, QByteArray content_type = "application/json") const;
    [[nodiscard]] QByteArray static_asset(const QString& path, QByteArray& content_type) const;
    [[nodiscard]] QByteArray import_zip(const Request& request, int& status) const;

    Workspace& workspace_;
    std::unique_ptr<QTcpServer> server_;
};

} // namespace muz
