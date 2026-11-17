#pragma once
#include "Transport.h"
#include "protocol/Protocol.h"
#include <QUdpSocket>
#include <memory>

namespace studio {
// 完全运行在所属事件循环线程中，不共享套接字，也不进行忙轮询。
class UdpTransport final : public Transport {
    Q_OBJECT
  public:
    explicit UdpTransport(std::unique_ptr<Protocol>, QObject* parent = nullptr);
    bool open(const DeviceSession&) override;
    void close() override;
    bool send(const CanFrame&) override;
    bool isOpen() const override { return socket_.state() == QAbstractSocket::BoundState; }
    int initialRequestDelayMs() const override { return 300; }
    quint16 localPort() const { return socket_.localPort(); }

  private:
    QUdpSocket socket_;
    std::unique_ptr<Protocol> protocol_;
    QHostAddress peer_;
};
} // 命名空间 studio
