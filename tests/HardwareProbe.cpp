#include "protocol/LegacyCanFdCodec.h"
#include "services/AsyncDeviceService.h"
#include "services/DeviceService.h"
#include "transport/UdpTransport.h"
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QHash>
#include <QTimer>
#include <algorithm>
#include <cstdio>
#include <limits>
#include <memory>
#include <numeric>

using namespace studio;

namespace {
struct Options {
    DeviceSession session;
    bool motion = false;
    bool inputCalibration = false;
    bool outputCalibration = false;
    int setDataRateMbps = 0;
    int durationMs = 5000;
    MotionProgram program;
};

void printLine(const QString& text) {
    const auto line = QDateTime::currentDateTime().toString("HH:mm:ss.zzz") + " " + text;
    std::fprintf(stdout, "%s\n", line.toUtf8().constData());
    std::fflush(stdout);
}

class HardwareProbe final : public QObject {
  public:
    HardwareProbe(Options options, QObject* parent = nullptr)
        : QObject(parent), options_(std::move(options)),
          device_(std::make_unique<UdpTransport>(std::make_unique<LegacyCanFdCodec>()), nullptr) {
        connect(&device_, &DeviceService::message, this, [](const QString& text) { printLine(text); });
        connect(&device_, &DeviceService::versionChanged, this, [this](const QString& version) {
            if (version != "—") {
                version_ = version;
                printLine("固件版本 " + version);
            }
        });
        connect(&device_, &DeviceService::responseFrame, this, [this](const CanFrame& frame) {
            ++responses_[frame.command];
            if (options_.inputCalibration && frame.command == code(Command::CalibrationReport) &&
                frame.payload.size() >= 2)
                ++calibrationReports_[LegacyCanFdCodec::readU16(frame.payload, 0)];
            printLine(QString("RX cmd=%1 channel=%2 motor=%3 bytes=%4 data=%5")
                          .arg(frame.command)
                          .arg(frame.channel)
                          .arg(frame.motor)
                          .arg(frame.payload.size())
                          .arg(QString::fromLatin1(frame.payload.toHex(' '))));
        });
        connect(&device_, &DeviceService::telemetryReceived, this, [this](const Telemetry& sample) {
            ++telemetryCount_;
            minPosition_ = std::min(minPosition_, double(sample.position));
            maxPosition_ = std::max(maxPosition_, double(sample.position));
            minVelocity_ = std::min(minVelocity_, double(sample.velocity));
            maxVelocity_ = std::max(maxVelocity_, double(sample.velocity));
            minTorque_ = std::min(minTorque_, double(sample.torque));
            maxTorque_ = std::max(maxTorque_, double(sample.torque));
            lastMotorTemperature_ = sample.motorTemperature;
            lastDriverTemperature_ = sample.driverTemperature;
        });
        connect(&device_, &DeviceService::stateChanged, this, [this] { stateChanged(); });
        connect(&device_, &DeviceService::canFdDataRateRequestSent, this,
                [this](quint8 requested, bool success) {
            if (options_.setDataRateMbps == 0)
                return;
            printLine(QString("TX cmd=37 channel=%1 motor=%2 dlc=1 data=%3 result=%4")
                          .arg(options_.session.address.channel)
                          .arg(options_.session.address.motor)
                          .arg(requested, 2, 16, QChar('0'))
                          .arg(success ? "sent" : "failed"));
            if (!success)
                stopAndFinish(false, "命令37发送失败");
        });
        connect(&device_, &DeviceService::canFdDataRateResponse, this,
                [this](quint8 requested, quint8 actual, bool success) {
            if (options_.setDataRateMbps == 0)
                return;
            printLine(QString("ACK cmd=37 requested=%1 Mbps actual=%2 Mbps match=%3")
                          .arg(requested)
                          .arg(actual)
                          .arg(success ? "yes" : "no"));
            stopAndFinish(success, success ? "命令37真机应答校验通过"
                                           : "命令37真机应答与请求不一致");
        });
        connect(&device_, &DeviceService::inputCalibrationCommandResponse, this,
                [this](quint16 result) {
            if (!options_.inputCalibration)
                return;
            printLine(QString("ACK cmd=3 result=%1").arg(result));
            if (result != 0)
                QTimer::singleShot(0, this, [this, result] {
                    stopAndFinish(false, QString("输入轴校准启动失败，错误码 %1").arg(result));
                });
        });
        connect(&device_, &DeviceService::inputCalibrationReportReceived, this,
                [this](quint16 step, quint16 data0, quint16 data1, quint16 data2) {
            if (!options_.inputCalibration)
                return;
            printLine(QString("CALIB cmd=31 step=%1 DATA0=%2 DATA1=%3 DATA2=%4")
                          .arg(step)
                          .arg(data0)
                          .arg(data1)
                          .arg(data2));
            if (step == 5)
                QTimer::singleShot(0, this, [this] {
                    stopAndFinish(true, "输入轴校准真机完成上报 step 5 已收到");
                });
        });

        safetyTimer_.setSingleShot(true);
        safetyTimer_.setInterval(options_.inputCalibration ? 60000
                                                          : options_.motion ? options_.durationMs + 10000
                                                                            : 6000);
        connect(&safetyTimer_, &QTimer::timeout, this, [this] { stopAndFinish(false, "真机探针总超时"); });
    }

    void start() {
        printLine(QString("连接 %1，数据波特率 %2")
                      .arg(options_.session.address.key())
                      .arg(options_.session.dataBitrate));
        safetyTimer_.start();
        device_.connectSession(options_.session);
    }

  private:
    enum class Phase { Connecting, DataRate, PassiveQueries, Calibrating, Enabling, Moving, Disabling, Finishing };

    void stateChanged() {
        if (finishing_)
            return;
        if (device_.connectionFailed()) {
            stopAndFinish(false, "设备通信失败");
            return;
        }
        if (phase_ == Phase::Connecting && device_.connected()) {
            if (options_.inputCalibration) {
                phase_ = Phase::Calibrating;
                printLine("按协议发送输入轴校准命令3：DLC=0；等待命令31的step 0..5主动上报");
                device_.calibrate(false);
                return;
            }
            if (options_.setDataRateMbps != 0) {
                phase_ = Phase::DataRate;
                printLine(QString("按协议发送命令37：DLC=1 Byte0=%1 Mbps")
                              .arg(options_.setDataRateMbps));
                device_.setCanFdDataRate(quint8(options_.setDataRateMbps));
                return;
            }
            phase_ = Phase::PassiveQueries;
            device_.command(Command::Status);
            device_.readParameters();
            device_.command(Command::Encoder);
            QTimer::singleShot(1200, this, [this] { passiveQueriesFinished(); });
            return;
        }
        if (phase_ == Phase::Enabling && device_.enabled()) {
            phase_ = Phase::Moving;
            printLine(QString("开始位置正弦：A=%1 rad, f=%2 Hz, C=%3 rad, Kp=%4, Kd=%5, %6 ms")
                          .arg(options_.program.amplitude)
                          .arg(options_.program.frequency)
                          .arg(options_.program.offset)
                          .arg(options_.program.target.kp)
                          .arg(options_.program.target.kd)
                          .arg(options_.durationMs));
            device_.startMotion(options_.program);
            QTimer::singleShot(options_.durationMs, this, [this] {
                if (finishing_ || phase_ != Phase::Moving)
                    return;
                phase_ = Phase::Disabling;
                device_.stopMotion();
                device_.setEnabled(false);
            });
            return;
        }
        if (phase_ == Phase::Disabling && !device_.enabled())
            stopAndFinish(telemetryCount_ > 0, telemetryCount_ > 0 ? "运动测试完成" : "没有收到运动反馈");
    }

    void passiveQueriesFinished() {
        if (finishing_ || phase_ != Phase::PassiveQueries)
            return;
        const bool versionOk = !version_.isEmpty() && responses_.value(code(Command::Version)) > 0;
        printLine(QString("只读检查：version=%1, status=%2, config=%3, encoder=%4")
                      .arg(responses_.value(code(Command::Version)))
                      .arg(responses_.value(code(Command::Status)))
                      .arg(responses_.value(code(Command::Config)))
                      .arg(responses_.value(code(Command::Encoder))));
        if (!versionOk) {
            stopAndFinish(false, "未收到合法版本应答");
            return;
        }
        if (!options_.motion) {
            stopAndFinish(true, "只读真机通信检查通过");
            return;
        }
        phase_ = Phase::Enabling;
        printLine("请求电机使能");
        device_.setEnabled(true);
    }

    void stopAndFinish(bool success, const QString& reason) {
        if (finishing_)
            return;
        finishing_ = true;
        phase_ = Phase::Finishing;
        safetyTimer_.stop();
        if (device_.connected()) {
            device_.stopMotion();
            if (device_.enabled())
                device_.setEnabled(false);
        }
        printLine(reason);
        if (options_.inputCalibration) {
            QStringList stages;
            for (auto it = calibrationReports_.cbegin(); it != calibrationReports_.cend(); ++it)
                stages.append(QString("step%1=%2").arg(it.key()).arg(it.value()));
            stages.sort();
            printLine("校准上报统计：" + (stages.isEmpty() ? QStringLiteral("无命令31上报")
                                                        : stages.join(", ")));
        }
        printLine(QString("汇总：version=%1, frames=%2, telemetry=%3, position=[%4,%5], velocity=[%6,%7], "
                          "torque=[%8,%9], motorTemp=%10, driverTemp=%11")
                      .arg(version_.isEmpty() ? "—" : version_)
                      .arg(std::accumulate(responses_.cbegin(), responses_.cend(), 0))
                      .arg(telemetryCount_)
                      .arg(telemetryCount_ ? minPosition_ : 0.0, 0, 'f', 4)
                      .arg(telemetryCount_ ? maxPosition_ : 0.0, 0, 'f', 4)
                      .arg(telemetryCount_ ? minVelocity_ : 0.0, 0, 'f', 4)
                      .arg(telemetryCount_ ? maxVelocity_ : 0.0, 0, 'f', 4)
                      .arg(telemetryCount_ ? minTorque_ : 0.0, 0, 'f', 4)
                      .arg(telemetryCount_ ? maxTorque_ : 0.0, 0, 'f', 4)
                      .arg(lastMotorTemperature_, 0, 'f', 2)
                      .arg(lastDriverTemperature_, 0, 'f', 2));
        device_.disconnectDevice();
        QCoreApplication::exit(success ? 0 : 2);
    }

    Options options_;
    DeviceService device_;
    QTimer safetyTimer_;
    QHash<quint8, int> responses_;
    QHash<quint16, int> calibrationReports_;
    QString version_;
    Phase phase_ = Phase::Connecting;
    bool finishing_ = false;
    int telemetryCount_ = 0;
    double minPosition_ = std::numeric_limits<double>::infinity();
    double maxPosition_ = -std::numeric_limits<double>::infinity();
    double minVelocity_ = std::numeric_limits<double>::infinity();
    double maxVelocity_ = -std::numeric_limits<double>::infinity();
    double minTorque_ = std::numeric_limits<double>::infinity();
    double maxTorque_ = -std::numeric_limits<double>::infinity();
    double lastMotorTemperature_ = 0;
    double lastDriverTemperature_ = 0;
};

class AsyncInputCalibrationProbe final : public QObject {
  public:
    explicit AsyncInputCalibrationProbe(Options options, QObject* parent = nullptr)
        : QObject(parent), options_(std::move(options)), device_(TransportKind::Udp) {
        connect(&device_, &AsyncDeviceService::message, this,
                [](const QString& text) { printLine(text); });
        connect(&device_, &AsyncDeviceService::versionChanged, this,
                [](const QString& version) {
            if (version != "—")
                printLine("固件版本 " + version);
        });
        connect(&device_, &AsyncDeviceService::responseFrame, this, [this](const CanFrame& frame) {
            ++frameCount_;
            if (frame.command == code(Command::CalibrationReport) && frame.payload.size() >= 2)
                ++reports_[LegacyCanFdCodec::readU16(frame.payload, 0)];
            printLine(QString("RX cmd=%1 channel=%2 motor=%3 bytes=%4 data=%5")
                          .arg(frame.command)
                          .arg(frame.channel)
                          .arg(frame.motor)
                          .arg(frame.payload.size())
                          .arg(QString::fromLatin1(frame.payload.toHex(' '))));
        });
        connect(&device_, &AsyncDeviceService::inputCalibrationCommandResponse, this,
                [this](quint16 result) {
            printLine(QString("ACK cmd=3 result=%1").arg(result));
            if (result != 0)
                finish(false, QString("输入轴校准启动失败，错误码 %1").arg(result));
        });
        connect(&device_, &AsyncDeviceService::outputCalibrationCommandResponse, this,
                [this](quint16 result) {
            if (!options_.outputCalibration)
                return;
            printLine(QString("ACK cmd=25 result=%1").arg(result));
            if (result != 0)
                finish(false, QString("输出轴校准启动失败，错误码 %1").arg(result));
        });
        connect(&device_, &AsyncDeviceService::inputCalibrationReportReceived, this,
                [this](quint16 step, quint16 data0, quint16 data1, quint16 data2) {
            printLine(QString("UI cmd=31 step=%1 DATA0=%2 DATA1=%3 DATA2=%4 busy=%5")
                          .arg(step)
                          .arg(data0)
                          .arg(data1)
                          .arg(data2)
                          .arg(device_.busy() ? "yes" : "no"));
            if (step == 5) {
                stepFive_ = true;
                QTimer::singleShot(0, this, [this] { checkCompletion(); });
            }
        });
        connect(&device_, &AsyncDeviceService::outputCalibrationResultReceived, this,
                [this](quint16 data0, quint16 data1, quint16 data2) {
            if (!options_.outputCalibration)
                return;
            stepSix_ = true;
            printLine(QString("UI cmd=31 step=6 DATA0=%1 DATA1=%2 DATA2=%3 busy=%4")
                          .arg(data0)
                          .arg(data1)
                          .arg(data2)
                          .arg(device_.busy() ? "yes" : "no"));
            QTimer::singleShot(0, this, [this] { checkCompletion(); });
        });
        connect(&device_, &AsyncDeviceService::stateChanged, this, [this] {
            printLine(QString("UI state connected=%1 enabled=%2 busy=%3 operation=%4")
                          .arg(device_.connected() ? "yes" : "no")
                          .arg(device_.enabled() ? "yes" : "no")
                          .arg(device_.busy() ? "yes" : "no")
                          .arg(device_.operation() == DeviceOperation::None
                                   ? QStringLiteral("idle")
                                   : deviceOperationKey(device_.operation())));
            if (device_.connectionFailed()) {
                finish(false, "设备通信失败");
                return;
            }
            if (device_.connected() && !started_) {
                started_ = true;
                printLine(options_.outputCalibration
                              ? "GUI异步服务发送输出轴校准命令25，等待命令31 step 6"
                              : "GUI异步服务发送输入轴校准命令3，等待命令31 step 5");
                device_.calibrate(options_.outputCalibration);
            }
            checkCompletion();
        });
        timeout_.setSingleShot(true);
        timeout_.setInterval(60000);
        connect(&timeout_, &QTimer::timeout, this,
                [this] { finish(false, "GUI异步校准真机测试超时"); });
    }

    void start() {
        printLine(QString("GUI异步链路连接 %1，数据波特率 %2")
                      .arg(options_.session.address.key())
                      .arg(options_.session.dataBitrate));
        timeout_.start();
        device_.connectSession(options_.session);
    }

  private:
    void checkCompletion() {
        if (options_.outputCalibration) {
            if (stepSix_ && !device_.busy())
                finish(true, "GUI异步链路已收到step 6且busy=false");
        } else if (stepFive_ && !device_.busy()) {
            finish(true, "GUI异步链路已收到step 5且busy=false");
        }
    }

    void finish(bool success, const QString& reason) {
        if (finishing_)
            return;
        finishing_ = true;
        timeout_.stop();
        QStringList stages;
        for (auto it = reports_.cbegin(); it != reports_.cend(); ++it)
            stages.append(QString("step%1=%2").arg(it.key()).arg(it.value()));
        stages.sort();
        printLine(reason);
        printLine("校准上报统计：" + stages.join(", "));
        printLine(QString("总接收帧数：%1").arg(frameCount_));
        device_.disconnectDevice();
        QCoreApplication::exit(success ? 0 : 2);
    }

    Options options_;
    AsyncDeviceService device_;
    QTimer timeout_;
    QHash<quint16, int> reports_;
    int frameCount_ = 0;
    bool started_ = false;
    bool stepFive_ = false;
    bool stepSix_ = false;
    bool finishing_ = false;
};
} // 匿名命名空间

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCommandLineParser parser;
    parser.setApplicationDescription("DEEP Robotics real-device protocol probe");
    parser.addHelpOption();
    parser.addOption({"ip", "Board IPv4 address", "ip", "192.168.1.121"});
    parser.addOption({"channel", "CAN channel 0..3", "channel", "0"});
    parser.addOption({"motor", "Motor ID 0..15", "motor", "1"});
    parser.addOption({"bitrate", "CAN FD data bitrate", "bitrate", "5000000"});
    parser.addOption({"set-data-rate", "Send command 37 with Byte0 set to 5 or 8 Mbps", "mbps", "0"});
    parser.addOption({"motion", "Enable and run the explicitly configured short motion test"});
    parser.addOption({"calibrate-input", "Send command 3 and capture real command-31 input calibration reports"});
    parser.addOption({"calibrate-output", "Send command 25 and capture the real command-31 step-6 result"});
    parser.addOption({"duration-ms", "Motion duration in milliseconds", "ms", "5000"});
    parser.addOption({"amplitude", "Position sine amplitude in rad", "value", "1.0"});
    parser.addOption({"frequency", "Position sine frequency in Hz", "value", "0.7"});
    parser.addOption({"offset", "Position sine offset in rad", "value", "0.0"});
    parser.addOption({"kp", "Position gain", "value", "50.0"});
    parser.addOption({"kd", "Velocity gain", "value", "5.0"});
    parser.process(app);

    bool okChannel = false, okMotor = false, okRate = false, okSetRate = false,
         okDuration = false;
    const int channel = parser.value("channel").toInt(&okChannel);
    const int motor = parser.value("motor").toInt(&okMotor);
    const int bitrate = parser.value("bitrate").toInt(&okRate);
    const int setDataRate = parser.value("set-data-rate").toInt(&okSetRate);
    const int duration = parser.value("duration-ms").toInt(&okDuration);
    const bool inputCalibration = parser.isSet("calibrate-input");
    const bool outputCalibration = parser.isSet("calibrate-output");
    if (!okChannel || channel < 0 || channel > 3 || !okMotor || motor < 0 || motor > 15 || !okRate ||
        (bitrate != 5000000 && bitrate != 8000000) || !okSetRate ||
        (setDataRate != 0 && setDataRate != 5 && setDataRate != 8) ||
        !okDuration || duration < 500 || duration > 10000 ||
        (setDataRate != 0 && (parser.isSet("motion") || inputCalibration || outputCalibration)) ||
        (parser.isSet("motion") && (inputCalibration || outputCalibration)) ||
        (inputCalibration && outputCalibration)) {
        std::fprintf(stderr, "invalid arguments\n");
        return 1;
    }

    Options options;
    options.session = {{parser.value("ip"), quint8(channel), quint8(motor)}, quint32(bitrate),
                       "deep.ethercan.canfd.v1"};
    options.motion = parser.isSet("motion");
    options.inputCalibration = inputCalibration;
    options.outputCalibration = outputCalibration;
    options.setDataRateMbps = setDataRate;
    options.durationMs = duration;
    options.program.waveform = Waveform::PositionSine;
    options.program.amplitude = parser.value("amplitude").toDouble();
    options.program.frequency = parser.value("frequency").toDouble();
    options.program.offset = parser.value("offset").toDouble();
    options.program.target.kp = parser.value("kp").toFloat();
    options.program.target.kd = parser.value("kd").toFloat();

    if (options.inputCalibration || options.outputCalibration) {
        AsyncInputCalibrationProbe probe(std::move(options));
        QTimer::singleShot(0, &probe, [&probe] { probe.start(); });
        return app.exec();
    }
    HardwareProbe probe(std::move(options));
    QTimer::singleShot(0, &probe, [&probe] { probe.start(); });
    return app.exec();
}
