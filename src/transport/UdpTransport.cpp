#include "UdpTransport.h"
#include "protocol/EtherCanV1Codec.h"
#include <QNetworkDatagram>
#include <QVariant>

namespace studio {

UdpTransport::UdpTransport(std::unique_ptr<Protocol> protocol, QObject* parent)
    : Transport(parent), protocol_(std::move(protocol)) {
    connect(&socket_, &QUdpSocket::readyRead, this, [this] {
        while (socket_.hasPendingDatagrams()) {
            const auto datagram = socket_.receiveDatagram(2048);
            // 即使网关使用不同的 UDP 源端口，已部署上位机也会接收所选板卡的应答。
            // 只按 IP 过滤既能隔离当前板卡，也不会丢弃这些有效的 CAN-FD 上行帧。
            if (datagram.senderAddress() != peer_)
                continue;
            const auto result = protocol_->decode(datagram.data());
            if (!result.ok()) {
                emit errorOccurred(result.error);
                continue;
            }
            for (const auto& frame : result.frames)
                emit frameReceived(frame);
        }
    });
}

bool UdpTransport::open(const DeviceSession& session) {
    close();
    if (session.protocolId != protocol_->id()) {
        emit errorOccurred(QStringLiteral("会话协议与通信适配器不匹配"));
        return false;
    }
    peer_ = QHostAddress(session.address.ip);
    if (peer_.protocol() != QAbstractSocket::IPv4Protocol) {
        emit errorOccurred(QStringLiteral("请输入有效 IPv4 地址"));
        return false;
    }
    if (!socket_.bind(QHostAddress::AnyIPv4, 0)) {
        emit errorOccurred(socket_.errorString());
        return false;
    }
    // 输入轴校准报告会以密集的命令 31 帧突发到达。
    // 即使事件循环线程被 Windows 短暂抢占，也要保留足够的内核队列空间
    // 来容纳完整突发数据。
    socket_.setSocketOption(QAbstractSocket::ReceiveBufferSizeSocketOption,
                            QVariant(4 * 1024 * 1024));

    const auto board = session.address.ip.section('.', 3).toUInt();
    // 与已部署老上位机完全一致：每个 EtherCAN 下行帧都使用 frame_id 1。
    // 老程序将每个数据包清零后只递增一次该局部字段，不维护跨包序号计数器。
    const auto packet = protocol_->configuration(quint8(board), session.dataBitrate, 1);
    if (packet.isEmpty() ||
        socket_.writeDatagram(packet, peer_, EtherCanV1Codec::BoardPort) != packet.size()) {
        emit errorOccurred(packet.isEmpty() ? QStringLiteral("通信板配置参数无效") : socket_.errorString());
        close();
        return false;
    }
    return true;
}

void UdpTransport::close() {
    socket_.close();
}

bool UdpTransport::send(const CanFrame& frame) {
    if (socket_.state() != QAbstractSocket::BoundState) {
        emit errorOccurred(QStringLiteral("UDP 尚未打开"));
        return false;
    }
    const auto data = protocol_->encode(frame, 1);
    if (data.isEmpty()) {
        emit errorOccurred(QStringLiteral("协议拒绝命令号或载荷长度无效的帧"));
        return false;
    }
    if (socket_.writeDatagram(data, peer_, EtherCanV1Codec::BoardPort) != data.size()) {
        emit errorOccurred(socket_.errorString());
        return false;
    }
    return true;
}

} // 命名空间 studio
