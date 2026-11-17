#include "DeviceSimulatorServer.h"
#include "protocol/CanFdFrameCodec.h"
#include "protocol/EtherCanV1Codec.h"
#include "protocol/LegacyCanFdCodec.h"
#include <QNetworkDatagram>
#include <QtEndian>
#include <cmath>

namespace studio {
using Codec = LegacyCanFdCodec;

DeviceSimulatorServer::DeviceSimulatorServer(QObject* parent) : QObject(parent) {
    connect(&socket_, &QUdpSocket::readyRead, this, &DeviceSimulatorServer::processPendingDatagrams);
    taskTimer_.setTimerType(Qt::PreciseTimer);
    taskTimer_.setInterval(10);
    connect(&taskTimer_, &QTimer::timeout, this, &DeviceSimulatorServer::advanceTasks);
}

bool DeviceSimulatorServer::listen(const QHostAddress& address, quint16 port) {
    close();
    if (!socket_.bind(address, port)) {
        emit protocolError(socket_.errorString());
        return false;
    }
    clock_.start();
    taskTimer_.start();
    return true;
}

void DeviceSimulatorServer::close() {
    taskTimer_.stop();
    socket_.close();
    motors_.clear();
    asynchronousSequence_ = 0;
}

quint32 DeviceSimulatorServer::stateKey(const CanFrame& frame) {
    return (quint32(frame.board) << 16) | (quint32(frame.channel) << 8) | frame.motor;
}

QByteArray DeviceSimulatorServer::defaultConfig(quint8 motor) {
    QByteArray config = Codec::floats({9.f}) + QByteArray(1, char(motor)) + QByteArray(1, char(10)) +
                        Codec::u16(1000) + Codec::u16(200) + Codec::u16(180) + Codec::u16(600) +
                        Codec::u16(850) + Codec::u16(800);
    config.resize(20); // DLC 11 承载 20 字节；字节 18..19 为保留字段。
    return config;
}

void DeviceSimulatorServer::processPendingDatagrams() {
    while (socket_.hasPendingDatagrams()) {
        const auto datagram = socket_.receiveDatagram(65535);
        EtherCanPacket packet;
        QString error;
        if (!EtherCanV1Codec::decode(datagram.data(), packet, error)) {
            emit protocolError(error);
            continue;
        }
        if (packet.frameType != EtherCanV1Codec::Download) {
            emit protocolError(QStringLiteral("模拟器收到的 EtherCAN 帧不是下行帧"));
            continue;
        }
        if (packet.command == EtherCanV1Codec::ConfigureCan) {
            if (packet.body.size() != 4 * 38) {
                emit protocolError(QStringLiteral("CAN 配置体长度不是 152 字节"));
                continue;
            }
            const auto board = quint8(packet.body[0]);
            const auto bitrate = qFromLittleEndian<quint32>(packet.body.constData() + 6);
            emit configured(board, bitrate);
            continue;
        }
        if (packet.command != EtherCanV1Codec::TransmitCan || packet.body.isEmpty() ||
            packet.body.size() % CanFdFrameCodec::TxContainerBytes != 0) {
            emit protocolError(QStringLiteral("模拟器收到的报文不是有效 CAN FD 下行容器"));
            continue;
        }
        for (int offset = 0; offset < packet.body.size(); offset += CanFdFrameCodec::TxContainerBytes) {
            CanFrame request;
            if (!CanFdFrameCodec::decodeTx(
                    packet.body.mid(offset, CanFdFrameCodec::TxContainerBytes), request, error)) {
                emit protocolError(error);
                continue;
            }
            emit requestReceived(request);
            processRequest(request, packet.sequence, datagram.senderAddress(), datagram.senderPort());
        }
    }
}

void DeviceSimulatorServer::sendResponse(const CanFrame& request, QByteArray payload, quint8 sequence,
                                         const QHostAddress& peerAddress, quint16 peerPort) {
    CanFrame response = request;
    response.payload = std::move(payload);
    const auto datagram = Codec{}.encodeResponse({response}, sequence);
    if (datagram.isEmpty() || socket_.writeDatagram(datagram, peerAddress, peerPort) != datagram.size()) {
        emit protocolError(datagram.isEmpty() ? QStringLiteral("模拟器无法编码应答") : socket_.errorString());
        return;
    }
    emit responseSent(response);
}

void DeviceSimulatorServer::processRequest(const CanFrame& request, quint8 sequence,
                                           const QHostAddress& peerAddress, quint16 peerPort) {
    auto& state = motors_[stateKey(request)];
    if (state.config.isEmpty())
        state.config = defaultConfig(request.motor);
    state.peerAddress = peerAddress;
    state.peerPort = peerPort;

    const auto respond = [&](QByteArray payload) {
        sendResponse(request, std::move(payload), sequence, peerAddress, peerPort);
    };
    switch (static_cast<Command>(request.command)) {
    case Command::Disable:
        state.enabled = false;
        state.target = {};
        respond(QByteArray(1, 0));
        break;
    case Command::Enable:
        state.enabled = true;
        respond(Codec::u16(0));
        break;
    case Command::CalibrateInput:
        state.calibrationStep = 0;
        respond(Codec::u16(0));
        break;
    case Command::Motion: {
        state.target = {Codec::readFloat(request.payload, 0), Codec::readFloat(request.payload, 4),
                        Codec::readFloat(request.payload, 8), Codec::readU16(request.payload, 12) / 10.f,
                        Codec::readU16(request.payload, 14) / 10.f};
        if (state.enabled)
            state.position += 0.08f * (state.target.position - state.position);
        const double t = clock_.elapsed() / 1000.;
        respond(Codec::floats({state.position, state.enabled ? state.target.velocity * 0.97f : 0.f,
                               state.enabled ? state.target.torque + float(0.015 * std::sin(t * 3)) : 0.f}) +
                Codec::u16(quint16((32 + 2 * std::sin(t * 0.1)) * 100)) +
                Codec::u16(quint16((29 + std::sin(t * 0.13)) * 100)));
        break;
    }
    case Command::Reset:
        state.enabled = false;
        state.target = {};
        respond(QByteArray(1, 0));
        break;
    case Command::SetHome:
        state.position = 0;
        state.target.position = 0;
        respond(QByteArray(1, 0));
        break;
    case Command::SetGear:
    case Command::SetId:
    case Command::SetTimeout:
    case Command::SetBandwidth:
    case Command::SetCurrent:
    case Command::SetUnderVoltage:
    case Command::SetOverVoltage:
    case Command::SetMotorTemperature:
    case Command::SetDriverTemperature: {
        static constexpr int offsets[] = {0, 4, 5, 6, 8, 10, 12, 14, 16};
        const int index = request.command - code(Command::SetGear);
        state.config.replace(offsets[index], request.payload.size(), request.payload);
        respond(request.payload);
        break;
    }
    case Command::SaveConfig:
    case Command::ClearError:
    case Command::FirmwareStart:
    case Command::FirmwareWrite:
    case Command::FirmwareBoot:
        respond(QByteArray(1, 0));
        break;
    case Command::FirmwareCheck:
        respond(QByteArray(1, 0));
        break;
    case Command::Version:
        respond(Codec::u16(3) + Codec::u16(118));
        break;
    case Command::Status:
        respond(Codec::u16(0));
        break;
    case Command::Config:
        respond(state.config);
        break;
    case Command::CalibrateOutput:
        state.outputCalibrationTicks = 0;
        respond(Codec::u16(0));
        break;
    case Command::Encoder: {
        const double t = clock_.elapsed() / 1000.;
        respond(Codec::floats({float(3.14159265 + 2.8 * std::sin(t * 0.5))}));
        break;
    }
    case Command::StopEncoder:
        respond(QByteArray(1, 0));
        break;
    case Command::GetCanId:
        // 命令 28 广播到目标 0x1f，模拟器测试不使用该命令。
        // 真机将使用自身节点 ID 应答。
        break;
    case Command::GetPhaseAngle:
        respond(Codec::u32(0) + Codec::floats({0.125f}));
        break;
    case Command::GetAngleError:
        respond(Codec::floats({state.position, state.position - 0.001f, 0.001f, 0.0572958f}));
        break;
    case Command::GetCalibration:
        respond(Codec::u32(1) + Codec::floats({0.f}));
        break;
    case Command::GetJointStatus:
        respond(Codec::u16(state.enabled ? 2 : 0) + Codec::u16(0) + Codec::floats({48.f}));
        break;
    case Command::MotionCrAsu:
        respond(Codec::floats({state.position, 0.f, 0.f}) + Codec::u16(3200) +
                Codec::u16(2900) + Codec::u16(4800) + Codec::u16(0));
        break;
    case Command::GetCanFdTxRxCount:
        respond(Codec::u32(0) + Codec::u32(0));
        break;
    case Command::SetCanFdDataRate:
        respond(request.payload.left(1));
        break;
    case Command::TestScope:
        respond(QByteArray(1, 1));
        break;
    case Command::TestStart:
        state.inspectionTicks = 0;
        respond(QByteArray(1, 1));
        break;
    case Command::TestStep:
        respond(QByteArray(1, char(qMin(7, state.inspectionTicks / 25 + 1))));
        break;
    case Command::TestData:
        respond(Codec::floats({0.0012f, -0.03f, 0.04f, 0.07f, 0.002f, -0.02f, 0.025f, 0.045f}));
        break;
    case Command::CalibrationReport:
        emit protocolError(QStringLiteral("命令 31 是仅上行报告，不能由上位机请求"));
        break;
    }
}

void DeviceSimulatorServer::advanceTasks() {
    for (auto it = motors_.begin(); it != motors_.end(); ++it) {
        auto& state = it.value();
        if (state.inspectionTicks >= 0)
            ++state.inspectionTicks;
        if (state.outputCalibrationTicks >= 0 && ++state.outputCalibrationTicks >= 24 &&
            state.peerPort != 0) {
            state.outputCalibrationTicks = -1;
            CanFrame report;
            report.board = quint8((it.key() >> 16) & 0xff);
            report.channel = quint8((it.key() >> 8) & 0xff);
            report.motor = quint8(it.key() & 0xff);
            report.command = code(Command::CalibrationReport);
            report.payload = Codec::u16(6) + QByteArray(6, 0);
            sendResponse(report, report.payload, asynchronousSequence_++, state.peerAddress,
                         state.peerPort);
        }
        if (state.calibrationStep < 0 || state.peerPort == 0)
            continue;

        CanFrame report;
        report.board = quint8((it.key() >> 16) & 0xff);
        report.channel = quint8((it.key() >> 8) & 0xff);
        report.motor = quint8(it.key() & 0xff);
        report.command = code(Command::CalibrationReport);
        const int n = state.calibrationStep++;
        if (n == 0)
            report.payload = Codec::u16(0) + QByteArray(6, 0);
        else if (n <= 128)
            report.payload = Codec::u16(2) + Codec::u16(0) +
                             Codec::floats({float(0.15 * std::sin(n * 0.08) + 0.02 * std::sin(n * 0.5))});
        else if (n <= 256)
            report.payload = Codec::u16(3) + Codec::u16(0) +
                             Codec::u32(quint32(qint32(30 * std::sin(n * 0.12))));
        else {
            state.calibrationStep = -1;
            report.payload = Codec::u16(5) + QByteArray(6, 0);
        }
        sendResponse(report, report.payload, asynchronousSequence_++, state.peerAddress, state.peerPort);
    }
}

} // 命名空间 studio
