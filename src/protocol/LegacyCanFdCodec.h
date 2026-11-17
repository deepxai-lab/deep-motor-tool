#pragma once
#include "Protocol.h"
#include <optional>

namespace studio {
class LegacyCanFdCodec final : public Protocol {
  public:
    QString id() const override { return "deep.ethercan.canfd.v1"; }
    QByteArray encode(const CanFrame&, quint8 sequence) const override;
    DecodeResult decode(const QByteArray&) const override;
    QByteArray configuration(quint8 board, quint32 dataBitrate, quint8 sequence) const override;
    // 用于回环测试和模拟器工具；上行帧使用 82 字节老格式容器。
    QByteArray encodeResponse(const QList<CanFrame>&, quint8 sequence = 0) const;
    static QByteArray motion(const Motion&);
    static std::optional<Telemetry> telemetry(const QByteArray&);
    static QByteArray floats(std::initializer_list<float>);
    static QByteArray u16(quint16);
    static QByteArray u32(quint32);
    static float readFloat(const QByteArray&, int offset);
    static qint32 readI32(const QByteArray&, int offset);
    static quint16 readU16(const QByteArray&, int offset);
    static quint32 crc32(const QByteArray&);
    static int dataLength(quint8 dlc);
};
} // 命名空间 studio
