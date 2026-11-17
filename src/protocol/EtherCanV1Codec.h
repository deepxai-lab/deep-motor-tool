#pragma once

#include <QByteArray>
#include <QString>

namespace studio {

struct EtherCanPacket {
    quint8 sequence = 0;
    quint8 frameType = 0;
    quint8 command = 0;
    QByteArray body;
};

class EtherCanV1Codec final {
  public:
    static constexpr quint16 Header = 0x5244;
    static constexpr quint8 Version = 1;
    static constexpr quint8 Download = 0x12;
    static constexpr quint8 Response = 0x21;
    static constexpr quint8 ConfigureCan = 0xa1;
    static constexpr quint8 TransmitCan = 0xa2;
    static constexpr quint8 ReceiveCan = 0xa3;
    static constexpr int HeaderBytes = 10;
    static constexpr quint16 BoardPort = 43893;

    static QByteArray encode(const QByteArray& body, quint8 command, quint8 sequence, quint8 frameType);
    static bool decode(const QByteArray&, EtherCanPacket&, QString& error);
    static QByteArray configurationBody(quint8 board, quint32 dataBitrate);
};

} // 命名空间 studio
