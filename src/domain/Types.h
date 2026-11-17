#pragma once
#include <QByteArray>
#include <QDateTime>
#include <QMetaType>
#include <QString>
#include <array>

namespace studio {
struct BoardProfile {
    QString ip = "192.168.1.121";
    quint32 dataBitrate = 5000000;
    QString protocolId = "deep.ethercan.canfd.v1";
};
struct DeviceAddress {
    QString ip = "192.168.1.121";
    quint8 channel = 0;
    quint8 motor = 1;
    bool operator==(const DeviceAddress&) const = default;
    QString key() const { return QString("%1 / CAN %2 / M%3").arg(ip).arg(channel).arg(motor); }
};
struct DeviceSession {
    DeviceAddress address;
    quint32 dataBitrate = 5000000;
    QString protocolId = "deep.ethercan.canfd.v1";
};
struct Motion {
    float position = 0, velocity = 0, torque = 0, kp = 0, kd = 0;
};
struct Telemetry {
    double seconds = 0;
    float position = 0, velocity = 0, torque = 0, motorTemperature = 0, driverTemperature = 0;
};
struct Identity {
    QString type = "03", deviceId, serial;
};
struct TestResult {
    std::array<float, 8> values{};
    QString result;
};
struct PhaseAngleData {
    qint32 phaseOrder = 0;
    float electricalAngleOffset = 0;
};
struct AngleErrorData {
    float referenceAngle = 0;
    float jointAngle = 0;
    float errorRadians = 0;
    float errorDegrees = 0;
};
struct CanFdCounters {
    quint32 received = 0;
    quint32 transmitted = 0;
};
struct CanFrame {
    quint8 board = 121, channel = 0, motor = 1, command = 0;
    QByteArray payload;
    // 主机接收时间仅用于诊断，绝不序列化进 EtherCAN/CAN-FD 帧，
    // 因而不会影响线上协议。
    QDateTime receivedAt;
};
enum class Waveform { Constant, PositionSine, TorqueSine };
enum class ParameterGroup { Motor, Protection };
// 主机侧工作流归属。此定义特意与 Command 分离：命令始终是独立协议帧，
// operation 只告知界面和空闲心跳策略当前哪个长时间工作流正在运行。
enum class DeviceOperation : quint8 { None, Calibration, Inspection, Parameters, Firmware };

inline QString deviceOperationKey(DeviceOperation operation) {
    switch (operation) {
    case DeviceOperation::Calibration: return QStringLiteral("calibration");
    case DeviceOperation::Inspection: return QStringLiteral("inspection");
    case DeviceOperation::Parameters: return QStringLiteral("parameters");
    case DeviceOperation::Firmware: return QStringLiteral("firmware");
    case DeviceOperation::None: return {};
    }
    return {};
}
struct MotionProgram {
    Motion target;
    Waveform waveform = Waveform::Constant;
    double amplitude = 1, frequency = 0.2, offset = 0;
};
enum class Command : quint8 {
    Disable = 1,
    Enable = 2,
    CalibrateInput = 3,
    Motion = 4,
    Reset = 5,
    SetHome = 6,
    SetGear = 7,
    SetId = 8,
    SetTimeout = 9,
    SetBandwidth = 10,
    SetCurrent = 11,
    SetUnderVoltage = 12,
    SetOverVoltage = 13,
    SetMotorTemperature = 14,
    SetDriverTemperature = 15,
    SaveConfig = 16,
    ClearError = 17,
    FirmwareStart = 18,
    FirmwareWrite = 19,
    FirmwareCheck = 20,
    FirmwareBoot = 21,
    Version = 22,
    Status = 23,
    Config = 24,
    CalibrateOutput = 25,
    Encoder = 26,
    StopEncoder = 27,
    GetCanId = 28,
    CalibrationReport = 31,
    TestScope = 32,
    TestStart = 33,
    TestStep = 34,
    TestData = 35,
    SetCanFdDataRate = 37,
    GetPhaseAngle = 38,
    GetAngleError = 39,
    GetCalibration = 50,
    GetJointStatus = 51,
    MotionCrAsu = 52,
    GetCanFdTxRxCount = 53
};
inline quint8 code(Command c) {
    return static_cast<quint8>(c);
}
} // 命名空间 studio
Q_DECLARE_METATYPE(studio::CanFrame)
Q_DECLARE_METATYPE(studio::DeviceAddress)
Q_DECLARE_METATYPE(studio::DeviceSession)
Q_DECLARE_METATYPE(studio::Telemetry)
Q_DECLARE_METATYPE(studio::Motion)
Q_DECLARE_METATYPE(studio::TestResult)
Q_DECLARE_METATYPE(studio::PhaseAngleData)
Q_DECLARE_METATYPE(studio::AngleErrorData)
Q_DECLARE_METATYPE(studio::CanFdCounters)
Q_DECLARE_METATYPE(studio::DeviceOperation)
