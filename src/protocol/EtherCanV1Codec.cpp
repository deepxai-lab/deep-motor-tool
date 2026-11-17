#include "EtherCanV1Codec.h"
#include <QtEndian>

namespace studio {
namespace {
void put16(QByteArray& bytes, int offset, quint16 value) {
    qToLittleEndian(value, bytes.data() + offset);
}
void put32(QByteArray& bytes, int offset, quint32 value) {
    qToLittleEndian(value, bytes.data() + offset);
}
quint16 read16(const QByteArray& bytes, int offset) {
    return qFromLittleEndian<quint16>(bytes.constData() + offset);
}
} // 匿名命名空间

QByteArray EtherCanV1Codec::encode(const QByteArray& body, quint8 command, quint8 sequence, quint8 frameType) {
    if (body.size() > 0xffff)
        return {};
    QByteArray header(HeaderBytes, '\0');
    put16(header, 0, Header);
    header[2] = char(sequence);
    header[3] = char(Version);
    header[4] = char(frameType);
    header[5] = char(command);
    put16(header, 6, quint16(body.size()));
    quint16 checksum = 0;
    for (unsigned char byte : body)
        checksum = quint16(checksum + byte);
    put16(header, 8, checksum);
    return header + body;
}

bool EtherCanV1Codec::decode(const QByteArray& bytes, EtherCanPacket& packet, QString& error) {
    if (bytes.size() < HeaderBytes) {
        error = QStringLiteral("UDP 报文短于 10 字节以太网帧头");
        return false;
    }
    if (read16(bytes, 0) != Header || quint8(bytes[3]) != Version) {
        error = QStringLiteral("以太网帧头或协议版本错误");
        return false;
    }
    const int bodyBytes = read16(bytes, 6);
    if (bytes.size() != HeaderBytes + bodyBytes) {
        error = QStringLiteral("以太网报文长度字段与实际长度不匹配");
        return false;
    }
    quint16 checksum = 0;
    for (int i = HeaderBytes; i < bytes.size(); ++i)
        checksum = quint16(checksum + quint8(bytes[i]));
    if (checksum != read16(bytes, 8)) {
        error = QStringLiteral("以太网报文载荷累加和错误");
        return false;
    }
    packet.sequence = quint8(bytes[2]);
    packet.frameType = quint8(bytes[4]);
    packet.command = quint8(bytes[5]);
    packet.body = bytes.mid(HeaderBytes);
    return true;
}

QByteArray EtherCanV1Codec::configurationBody(quint8 board, quint32 dataBitrate) {
    if (dataBitrate != 5000000 && dataBitrate != 8000000)
        return {};
    constexpr int recordBytes = 38;
    QByteArray body(4 * recordBytes, '\0');
    for (int channel = 0; channel < 4; ++channel) {
        const int offset = channel * recordBytes;
        body[offset] = char(board);
        body[offset + 1] = char(channel);
    put32(body, offset + 2, 1000000); // 标称、仲裁比特率
        put32(body, offset + 6, dataBitrate);
    body[offset + 23] = 1; // CanfdConfig.buf.enable_canfd（老 ABI 偏移 21）
    body[offset + 27] = 1; // CanfdConfig.buf.enable_tdc（老 ABI 偏移 25）
    }
    return body;
}

} // 命名空间 studio
