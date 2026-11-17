#include "CanFdFrameCodec.h"
#include "CommandCatalog.h"
#include <QtEndian>

namespace studio {
namespace {
void put32(QByteArray& bytes, int offset, quint32 value) {
    qToLittleEndian(value, bytes.data() + offset);
}
quint32 read32(const QByteArray& bytes, int offset) {
    return qFromLittleEndian<quint32>(bytes.constData() + offset);
}
} // 匿名命名空间

QByteArray CanFdFrameCodec::encode(const CanFrame& frame, int containerBytes, bool response) {
    const bool canIdBroadcast = !response && frame.command == code(Command::GetCanId) && frame.motor == 0x1f;
    if (frame.channel > 3 || (frame.motor > 15 && !canIdBroadcast) || frame.command > 63 ||
        frame.payload.size() > 64)
        return {};
    const int dlc = canFdDlcForLength(frame.payload.size());
    if (dlc < 0)
        return {};

    QByteArray bytes(containerBytes, '\0');
    bytes[0] = char(frame.board);
    bytes[1] = char(frame.channel);
    put32(bytes, 2, quint32(frame.motor) | (quint32(frame.command) << 5) | (response ? 0x10u : 0u));
    put32(bytes, 6, quint32(dlc) | 0x30u); // bit4 为 BRS，bit5 为 CAN FD
    bytes.replace(10, frame.payload.size(), frame.payload);
    return bytes;
}

QByteArray CanFdFrameCodec::encodeTx(const CanFrame& frame) {
    return encode(frame, TxContainerBytes, false);
}

QByteArray CanFdFrameCodec::encodeRx(const CanFrame& frame) {
    return encode(frame, RxContainerBytes, true);
}

bool CanFdFrameCodec::decode(const QByteArray& bytes, int containerBytes, bool response,
                             CanFrame& frame, QString& error) {
    if (bytes.size() != containerBytes) {
        error = response ? QStringLiteral("CAN FD 上行容器长度不是 82 字节")
                         : QStringLiteral("CAN FD 下行容器长度不是 74 字节");
        return false;
    }
    const quint32 id = read32(bytes, 2);
    const quint32 flags = read32(bytes, 6);
    const auto dlc = quint8(flags & 0x0f);
    const int payloadBytes = canFdLength(dlc);
    if (payloadBytes < 0) {
        error = QStringLiteral("不支持的 CAN FD 帧格式");
        return false;
    }
    const auto command = quint8((id & 0x7ffu) >> 5);
    const bool canIdBroadcast = command == code(Command::GetCanId) && (id & 0x1fu) == 0x1fu;
    if (!response) {
        // 下行帧由本程序生成，必须保持已部署 ABI 完全一致。
        // 上行帧不同：老程序直接读取 id、dlc、data，不要求 BRS、CAN-FD
        // 或 bit4 应答标记。真机校准报告采用这种老格式，因此接收侧若强制
        // 校验仅属于下行帧的标志，会错误丢弃有效数据。
        if (id > 0x7ff || quint8(bytes[1]) > 3 || (flags & 0xc0u) ||
            (flags & 0x30u) != 0x30u) {
            error = QStringLiteral("不支持的 CAN FD 帧格式");
            return false;
        }
        if ((id & 0x10u) && !canIdBroadcast) {
            error = QStringLiteral("CAN FD 下行帧错误设置驱动器返回标志 bit4");
            return false;
        }
    }

    frame.board = quint8(bytes[0]);
    frame.channel = quint8(bytes[1]);
    frame.motor = canIdBroadcast ? quint8(0x1f) : quint8(id & 0x0fu);
    frame.command = command;
    frame.payload = bytes.mid(10, payloadBytes);
    const bool valid = response ? validResponsePayload(frame.command, frame.payload.size())
                                : validRequestPayload(frame.command, frame.payload.size());
    if (!valid) {
        error = response ? QStringLiteral("CAN FD 应答 DLC 与命令目录不匹配")
                         : QStringLiteral("CAN FD 请求 DLC 与命令目录不匹配");
        return false;
    }
    return true;
}

bool CanFdFrameCodec::decodeTx(const QByteArray& bytes, CanFrame& frame, QString& error) {
    return decode(bytes, TxContainerBytes, false, frame, error);
}

bool CanFdFrameCodec::decodeRx(const QByteArray& bytes, CanFrame& frame, QString& error) {
    return decode(bytes, RxContainerBytes, true, frame, error);
}

} // 命名空间 studio
