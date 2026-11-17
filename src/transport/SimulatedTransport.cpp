#include "SimulatedTransport.h"
#include "protocol/LegacyCanFdCodec.h"
#include <cmath>

namespace studio {
using Codec = LegacyCanFdCodec;
SimulatedTransport::SimulatedTransport(QObject* p) : Transport(p) {
    timer_.setInterval(20);
    connect(&timer_, &QTimer::timeout, this, &SimulatedTransport::tick);
}
bool SimulatedTransport::open(const DeviceSession& session) {
    close();
    if (session.protocolId != "deep.ethercan.canfd.v1" ||
        (session.dataBitrate != 5000000 && session.dataBitrate != 8000000))
        return false;
    address_ = session.address;
    clock_.start();
    timer_.start();
    config_ = Codec::floats({9.f}) + QByteArray(1, char(address_.motor)) + QByteArray(1, char(10)) +
              Codec::u16(1000) + Codec::u16(200) + Codec::u16(180) + Codec::u16(600) + Codec::u16(850) +
              Codec::u16(800);
    return true;
}
void SimulatedTransport::close() {
    timer_.stop();
    enabled_ = false;
    target_ = {};
    position_ = 0;
    calibration_ = outputCalibration_ = test_ = -1;
}
void SimulatedTransport::reply(quint8 command, QByteArray p) {
    CanFrame f;
    f.board = quint8(address_.ip.section('.', 3).toUInt());
    f.channel = address_.channel;
    f.motor = address_.motor;
    f.command = command;
    f.payload = std::move(p);
    emit frameReceived(f);
}
bool SimulatedTransport::send(const CanFrame& f) {
    if (!timer_.isActive() || f.channel != address_.channel || f.motor != address_.motor)
        return false;
    switch (static_cast<Command>(f.command)) {
    case Command::Enable:
        enabled_ = true;
        reply(f.command, Codec::u16(0));
        break;
    case Command::Disable:
        enabled_ = false;
        target_ = {};
        reply(f.command, QByteArray(1, 0));
        break;
    case Command::Motion:
        if (f.payload.size() >= 16) {
            target_ = {Codec::readFloat(f.payload, 0), Codec::readFloat(f.payload, 4),
                       Codec::readFloat(f.payload, 8), Codec::readU16(f.payload, 12) / 10.f,
                       Codec::readU16(f.payload, 14) / 10.f};
            // 真驱动器收到命令 4 后返回命令 4 反馈，此处保持相同行为。
            // 发送器停止或电机失能后，不能仅因模拟器维护定时器仍在运行，
            // 就继续产生虚假的运动帧。
            if (enabled_) {
                position_ += 0.025f * (target_.position - position_);
                const double t = clock_.elapsed() / 1000.;
                reply(code(Command::Motion),
                      Codec::floats({position_, target_.velocity * 0.97f,
                                     target_.torque + float(0.015 * std::sin(t * 3))}) +
                          Codec::u16(quint16((32 + 2 * std::sin(t * 0.1)) * 100)) +
                          Codec::u16(quint16((29 + std::sin(t * 0.13)) * 100)));
            }
        }
        break;
    case Command::Version:
        reply(f.command, Codec::u16(3) + Codec::u16(118));
        break;
    case Command::Status:
        reply(f.command, Codec::u16(0));
        break;
    case Command::Config:
        reply(f.command, config_);
        break;
    case Command::SetHome:
        position_ = 0;
        target_.position = 0;
        reply(f.command, QByteArray(1, 0));
        break;
    case Command::Reset:
        enabled_ = false;
        target_ = {};
        reply(f.command, QByteArray(1, 0));
        break;
    case Command::Encoder:
        reply(f.command, Codec::floats(
                             {float(3.14159265 + 2.8 * std::sin(clock_.elapsed() / 2000.))}));
        break;
    case Command::StopEncoder:
        reply(f.command, QByteArray(1, 0));
        break;
    case Command::GetPhaseAngle:
        reply(f.command, Codec::u32(0) + Codec::floats({0.125f}));
        break;
    case Command::GetAngleError:
        reply(f.command, Codec::floats({position_, position_ - 0.001f, 0.001f, 0.0572958f}));
        break;
    case Command::GetCanFdTxRxCount:
        reply(f.command, Codec::u32(1234) + Codec::u32(5678));
        break;
    case Command::SetCanFdDataRate:
        reply(f.command, f.payload.left(1));
        break;
    case Command::CalibrateInput:
        calibration_ = 0;
        reply(f.command, Codec::u16(0));
        break;
    case Command::CalibrateOutput:
        outputCalibration_ = 0;
        reply(f.command, Codec::u16(0));
        break;
    case Command::TestScope:
        reply(f.command, QByteArray(1, 1));
        break;
    case Command::TestStart:
        test_ = 0;
        reply(f.command, QByteArray(1, 1));
        break;
    case Command::TestStep:
        reply(f.command, QByteArray(1, char(qMin(7, test_ / 25 + 1))));
        break;
    case Command::TestData:
        reply(f.command, Codec::floats({0.0012f, -0.03f, 0.04f, 0.07f, 0.002f, -0.02f, 0.025f, 0.045f}));
        break;
    default:
        if (f.command >= 7 && f.command <= 15) {
            const int offsets[] = {0, 4, 5, 6, 8, 10, 12, 14, 16};
            const int sizes[] = {4, 1, 1, 2, 2, 2, 2, 2, 2};
            const int i = f.command - 7;
            if (f.payload.size() >= sizes[i])
                config_.replace(offsets[i], sizes[i], f.payload.left(sizes[i]));
            reply(f.command, f.payload.left(sizes[i]));
        } else {
            reply(f.command, QByteArray(1, 0));
        }
        break;
    }
    return true;
}
void SimulatedTransport::tick() {
    if (outputCalibration_ >= 0 && ++outputCalibration_ >= 12) {
        outputCalibration_ = -1;
        reply(code(Command::CalibrationReport), Codec::u16(6) + QByteArray(6, 0));
    }
    if (calibration_ >= 0) {
        int n = calibration_++;
        if (n == 0)
            reply(code(Command::CalibrationReport), Codec::u16(0) + QByteArray(6, 0));
        else if (n <= 128)
            reply(code(Command::CalibrationReport),
                  Codec::u16(2) + Codec::u16(0) +
                      Codec::floats({float(0.15 * std::sin(n * 0.08) + 0.02 * std::sin(n * 0.5))}));
        else if (n <= 256)
            reply(code(Command::CalibrationReport),
                  Codec::u16(3) + Codec::u16(0) + Codec::u32(quint32(qint32(30 * std::sin(n * 0.12)))));
        else {
            calibration_ = -1;
            reply(code(Command::CalibrationReport), Codec::u16(5) + QByteArray(6, 0));
        }
    }
    if (test_ >= 0)
        ++test_;
}
} // 命名空间 studio
