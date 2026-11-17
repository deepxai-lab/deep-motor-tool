#include "LegacyCanFdCodec.h"
#include "CanFdFrameCodec.h"
#include "CommandCatalog.h"
#include "EtherCanV1Codec.h"
#include <QtEndian>
#include <bit>
#include <cmath>
#include <limits>

namespace studio {
namespace {
quint32 read32(const QByteArray& bytes, int offset) {
    return qFromLittleEndian<quint32>(bytes.constData() + offset);
}
} // 匿名命名空间

int LegacyCanFdCodec::dataLength(quint8 dlc) {
    return canFdLength(dlc);
}

QByteArray LegacyCanFdCodec::encode(const CanFrame& frame, quint8 sequence) const {
    if (!validRequestPayload(frame.command, frame.payload.size()))
        return {};
    const auto body = CanFdFrameCodec::encodeTx(frame);
    return body.isEmpty() ? QByteArray{}
                          : EtherCanV1Codec::encode(body, EtherCanV1Codec::TransmitCan, sequence,
                                                    EtherCanV1Codec::Download);
}

QByteArray LegacyCanFdCodec::encodeResponse(const QList<CanFrame>& frames, quint8 sequence) const {
    QByteArray body;
    for (const auto& frame : frames) {
        const auto bytes = CanFdFrameCodec::encodeRx(frame);
        if (bytes.isEmpty())
            return {};
        body += bytes;
    }
    return EtherCanV1Codec::encode(body, EtherCanV1Codec::ReceiveCan, sequence, EtherCanV1Codec::Response);
}

DecodeResult LegacyCanFdCodec::decode(const QByteArray& datagram) const {
    auto fail = [](QString reason) { return DecodeResult{{}, std::move(reason)}; };
    EtherCanPacket packet;
    QString error;
    if (!EtherCanV1Codec::decode(datagram, packet, error))
        return fail(error);
    if (packet.frameType != EtherCanV1Codec::Response || packet.command != EtherCanV1Codec::ReceiveCan)
        return fail(QStringLiteral("收到的 UDP 报文不是 CAN FD 上行应答"));
    if (packet.body.isEmpty() || packet.body.size() > 1024 ||
        packet.body.size() % CanFdFrameCodec::RxContainerBytes != 0)
        return fail(QStringLiteral("CAN FD 上行容器总长度无效"));

    DecodeResult result;
    QString firstContainerError;
    for (int offset = 0; offset < packet.body.size(); offset += CanFdFrameCodec::RxContainerBytes) {
        CanFrame frame{};
        if (!CanFdFrameCodec::decodeRx(packet.body.mid(offset, CanFdFrameCodec::RxContainerBytes), frame,
                                       error)) {
            if (firstContainerError.isEmpty())
                firstContainerError = error;
            continue;
        }
        result.frames.push_back(frame);
    }
    if (result.frames.isEmpty() && !firstContainerError.isEmpty())
        return fail(firstContainerError);
    return result;
}

QByteArray LegacyCanFdCodec::configuration(quint8 board, quint32 dataBitrate, quint8 sequence) const {
    const auto body = EtherCanV1Codec::configurationBody(board, dataBitrate);
    return body.isEmpty() ? QByteArray{}
                          : EtherCanV1Codec::encode(body, EtherCanV1Codec::ConfigureCan, sequence,
                                                    EtherCanV1Codec::Download);
}

QByteArray LegacyCanFdCodec::u16(quint16 value) {
    QByteArray bytes(2, '\0');
    qToLittleEndian(value, bytes.data());
    return bytes;
}

QByteArray LegacyCanFdCodec::u32(quint32 value) {
    QByteArray bytes(4, '\0');
    qToLittleEndian(value, bytes.data());
    return bytes;
}

QByteArray LegacyCanFdCodec::floats(std::initializer_list<float> values) {
    QByteArray bytes;
    for (float value : values)
        bytes += u32(std::bit_cast<quint32>(value));
    return bytes;
}

quint16 LegacyCanFdCodec::readU16(const QByteArray& bytes, int offset) {
    return offset >= 0 && offset + 2 <= bytes.size()
               ? qFromLittleEndian<quint16>(bytes.constData() + offset)
               : 0;
}

float LegacyCanFdCodec::readFloat(const QByteArray& bytes, int offset) {
    return offset >= 0 && offset + 4 <= bytes.size()
               ? std::bit_cast<float>(read32(bytes, offset))
               : std::numeric_limits<float>::quiet_NaN();
}

qint32 LegacyCanFdCodec::readI32(const QByteArray& bytes, int offset) {
    return offset >= 0 && offset + 4 <= bytes.size() ? qint32(read32(bytes, offset)) : 0;
}

QByteArray LegacyCanFdCodec::motion(const Motion& motion) {
    if (!std::isfinite(motion.position) || !std::isfinite(motion.velocity) || !std::isfinite(motion.torque) ||
        !std::isfinite(motion.kp) || !std::isfinite(motion.kd) || motion.kp < 0 || motion.kd < 0 ||
        motion.kp > 6553.5f || motion.kd > 6553.5f)
        return {};
    // 严格保持已部署 Windows ABI。MotionData 先以 uint16_t 保存两个增益，
    // NetCanBoard 再乘以十，因此小数部分会先被丢弃（2.5 -> 20，0.5 -> 0）。
    return floats({motion.position, motion.velocity, motion.torque}) +
           u16(quint16(motion.kp) * 10) + u16(quint16(motion.kd) * 10);
}

std::optional<Telemetry> LegacyCanFdCodec::telemetry(const QByteArray& bytes) {
    if (bytes.size() < 16)
        return std::nullopt;
    Telemetry telemetry;
    telemetry.position = readFloat(bytes, 0);
    telemetry.velocity = readFloat(bytes, 4);
    telemetry.torque = readFloat(bytes, 8);
    telemetry.motorTemperature = qint16(readU16(bytes, 12)) / 100.f;
    telemetry.driverTemperature = qint16(readU16(bytes, 14)) / 100.f;
    if (!std::isfinite(telemetry.position) || !std::isfinite(telemetry.velocity) ||
        !std::isfinite(telemetry.torque))
        return std::nullopt;
    return telemetry;
}

quint32 LegacyCanFdCodec::crc32(const QByteArray& data) {
    quint32 crc = 0;
    for (unsigned char byte : data) {
        crc ^= quint32(byte) << 24;
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc & 0x80000000u) ? (crc << 1) ^ 0x04c11db7u : crc << 1;
    }
    return crc;
}

} // 命名空间 studio
