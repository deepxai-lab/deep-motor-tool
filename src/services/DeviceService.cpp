#include "DeviceService.h"
#include "protocol/LegacyCanFdCodec.h"
#include "services/HostConfiguration.h"
#include "transport/TransportFactory.h"
#include <QFile>
#include <cmath>
#include <limits>

namespace studio {
using Codec = LegacyCanFdCodec;
namespace {
QStringList legacyInspectionErrors(quint16 word) {
    QStringList errors;
    if (word & 0x0001) errors << QStringLiteral("过压保护");
    if (word & 0x0002) errors << QStringLiteral("欠压保护");
    if (word & 0x0004) errors << QStringLiteral("过流保护");
    if (word & 0x0008) errors << QStringLiteral("电机过温保护");
    if (word & 0x0010) errors << QStringLiteral("驱动过温保护");
    if (word & 0x0020) errors << QStringLiteral("CAN 超时");
    if (word & 0x0040) errors << QStringLiteral("温感线断连");
    // 量产下线流程沿用已部署上位机的测试专用位定义。
    // 此处特意与描述普通 V3 状态显示的 StatusWordCatalog 保持独立。
    if (word & 0x0080) errors << QStringLiteral("输出端编码器校准失败");
    if (word & 0x0100) errors << QStringLiteral("输出端编码器离线");
    if (word & 0x0200) errors << QStringLiteral("CAN 通信断连");
    if (word & 0x0400) errors << QStringLiteral("输入端编码器零点异常");
    if (word & 0x0800) errors << QStringLiteral("输入端编码器线性度异常");
    if (word & 0x1000) errors << QStringLiteral("速度扭矩波动异常");
    if (word & 0x2000) errors << QStringLiteral("位置扭矩波动异常");
    if (word & 0x4000) errors << QStringLiteral("角度误差过大");
    return errors;
}
}

// ---------------- 会话生命周期与周期调度器 ----------------
DeviceService::DeviceService(QObject* p)
    : DeviceService(TransportFactory::create(TransportKind::Simulation), p) {}

DeviceService::DeviceService(std::unique_ptr<Transport> transport, QObject* p)
    : QObject(p), transport_(std::move(transport)) {
    Q_ASSERT(transport_);
    connect(transport_.get(), &Transport::frameReceived, this, &DeviceService::receive);
    connect(transport_.get(), &Transport::errorOccurred, this, &DeviceService::message);
    handshakeTimer_.setSingleShot(true);
    handshakeTimer_.setInterval(2200);
    connect(&handshakeTimer_, &QTimer::timeout, this, [this] {
        if (!connecting())
            return;
        failConnection("通信建立失败 · 版本应答超时");
    });
    // 已部署的 EtherCAN 板在收到 CAN 配置数据报后需要短暂稳定时间。
    // 老上位机在用户添加、选择电机时自然形成了这段延时，因此每隔 300 ms
    // 查询一次，直到收到首个有效版本应答，避免在同一事件轮次立即发送。
    handshakeQueryTimer_.setInterval(300);
    connect(&handshakeQueryTimer_, &QTimer::timeout, this, [this] {
        if (connecting())
            send(Command::Version);
    });
    // 产品界面要求空闲链路、版本探测频率为 20 Hz。
    // 业务流运行期间独占通信，因此运动、检测、校准或其他采集期间，
    // 此定时器不得插入命令 22。
    heartbeatTimer_.setInterval(50);
    connect(&heartbeatTimer_, &QTimer::timeout, this, [this] {
        const bool businessActive = busy() || motionTimer_.isActive() || linearity_ ||
                                    phaseAngleTimer_.isActive() || angleErrorTimer_.isActive();
        if (businessActive)
            return;
        send(Command::Version);
    });
    responseWatchdog_.setSingleShot(true);
    responseWatchdog_.setInterval(4500);
    connect(&responseWatchdog_, &QTimer::timeout, this, [this] {
        if (!connected())
            return;
        // 已部署上位机不会因业务命令缺少应答就判定通信断开。
        // 只有独立业务循环停止后，才恢复空闲版本看门狗。
        const bool businessActive = busy() || motionTimer_.isActive() || linearity_ ||
                                    phaseAngleTimer_.isActive() || angleErrorTimer_.isActive();
        if (businessActive || awaitingPostInspectionHeartbeat_) {
            responseWatchdog_.start();
            return;
        }
        failConnection("通信中断 · 设备应答超时");
    });
    // 老上位机将该值称为“刷新率”：统计一秒内有效的命令 4 电机应答，
    // 发布统计值后将计数器清零。
    refreshRateTimer_.setInterval(1000);
    connect(&refreshRateTimer_, &QTimer::timeout, this, [this] {
        emit refreshRateChanged(connected() ? motionResponsesSinceRateTick_ : 0);
        motionResponsesSinceRateTick_ = 0;
    });
    refreshRateTimer_.start();
    parameterTimer_.setSingleShot(true);
    parameterTimer_.setInterval(100);
    connect(&parameterTimer_, &QTimer::timeout, this, &DeviceService::sendNextParameterCommand);
    parameterResponseTimer_.setSingleShot(true);
    parameterResponseTimer_.setInterval(1500);
    connect(&parameterResponseTimer_, &QTimer::timeout, this, [this] {
        if (parameterSavePending_)
            failParameterWrite(QStringLiteral("参数写入失败 · 未收到命令 16 保存应答"));
    });
    // 已部署的 Windows 升级程序每隔 1000 / SEND_HZ 毫秒发送一个八字节固件块；
    // 该版本中的 SEND_HZ 为 500。
    firmwareTimer_.setSingleShot(true);
    firmwareTimer_.setTimerType(Qt::PreciseTimer);
    firmwareTimer_.setInterval(2);
    connect(&firmwareTimer_, &QTimer::timeout, this, &DeviceService::sendNextFirmwareBlock);
    firmwareTimeoutTimer_.setSingleShot(true);
    firmwareTimeoutTimer_.setInterval(3000);
    connect(&firmwareTimeoutTimer_, &QTimer::timeout, this, [this] {
        if (firmwareStage_ == FirmwareStage::WaitingErase)
            failFirmwareUpdate("固件升级失败 · 等待擦除应答超时");
        else if (firmwareStage_ == FirmwareStage::WaitingCheck)
            failFirmwareUpdate("固件升级失败 · 等待校验应答超时");
    });
    firmwarePostBootTimer_.setSingleShot(true);
    firmwarePostBootTimer_.setInterval(200);
    connect(&firmwarePostBootTimer_, &QTimer::timeout, this, [this] {
        if (firmwareStage_ != FirmwareStage::WaitingRebootQuery)
            return;
        // 与已部署升级程序一致：命令 21 后等待 200 ms，随后只发送一次
        // 版本查询，再结束升级工作流。
        send(Command::Version);
        // 从这次明确的老版本查询重新启动空闲 20 Hz 节拍，避免同一事件轮次
        // 的心跳超时再附加一帧。
        heartbeatTimer_.start();
        firmwareStage_ = FirmwareStage::Idle;
        if (operation_ == DeviceOperation::Firmware)
            setOperation(DeviceOperation::None);
    });
    // 已部署的 Windows 程序以 500 Hz 发送运动和编码器请求。
    // 保持相同的主机侧 2 ms 节拍；曲线重绘另行限频。
    motionTimer_.setTimerType(Qt::PreciseTimer);
    motionTimer_.setInterval(2);
    connect(&motionTimer_, &QTimer::timeout, this, [this] {
        sendMotionFrame(clock_.nsecsElapsed() / 1.0e9);
    });
    encoderTimer_.setTimerType(Qt::PreciseTimer);
    encoderTimer_.setInterval(2);
    connect(&encoderTimer_, &QTimer::timeout, this, [this] { send(Command::Encoder); });
    const auto queryRates = HostConfiguration::periodicQueryRates();
    phaseAngleQueryHz_ = queryRates.phaseAngleHz;
    angleErrorQueryHz_ = queryRates.angleErrorHz;
    phaseAngleTimer_.setTimerType(Qt::PreciseTimer);
    phaseAngleTimer_.setInterval(qMax(1, qRound(1000.0 / phaseAngleQueryHz_)));
    connect(&phaseAngleTimer_, &QTimer::timeout, this,
            [this] { send(Command::GetPhaseAngle); });
    angleErrorTimer_.setTimerType(Qt::PreciseTimer);
    angleErrorTimer_.setInterval(qMax(1, qRound(1000.0 / angleErrorQueryHz_)));
    connect(&angleErrorTimer_, &QTimer::timeout, this,
            [this] { send(Command::GetAngleError); });
    // 与已部署程序的下线检测循环一致：查询当前步骤，等待 500 ms，
    // 查询八项反馈值，再等待 500 ms 后进入下一步。STATUSWORD 每秒独立轮询一次。
    pollTimer_.setInterval(1000);
    connect(&pollTimer_, &QTimer::timeout, this, [this] {
        send(Command::TestStep);
        QTimer::singleShot(500, this, [this] {
            if (operation_ == DeviceOperation::Inspection)
                send(Command::TestData);
        });
    });
    inspectionStartTimer_.setInterval(500);
    connect(&inspectionStartTimer_, &QTimer::timeout, this, [this] {
        if (operation_ == DeviceOperation::Inspection)
            send(Command::TestStart);
    });
    inspectionStatusTimer_.setInterval(1000);
    connect(&inspectionStatusTimer_, &QTimer::timeout, this, [this] {
        if (operation_ == DeviceOperation::Inspection)
            send(Command::Status);
    });
}
void DeviceService::setOperation(DeviceOperation operation) {
    operation_ = operation;
    emit stateChanged();
}
void DeviceService::connectDevice(DeviceAddress a) {
    connectSession(DeviceSession{std::move(a)});
}
void DeviceService::connectSession(DeviceSession session) {
    disconnectDevice();
    session_ = session;
    address_ = session.address;
    safetyDisablePending_ = false;
    clock_.start();
    connectionState_ = ConnectionState::Connecting;
    emit stateChanged();
    emit message("正在建立通信 · " + address_.key());
    if (!transport_->open(session)) {
        connectionState_ = ConnectionState::Failed;
        emit stateChanged();
        emit message("通信建立失败 · 无法打开 UDP 会话");
        return;
    }
    handshakeTimer_.start();
    if (const int delay = transport_->initialRequestDelayMs(); delay > 0) {
        handshakeQueryTimer_.setInterval(delay);
        handshakeQueryTimer_.start();
    } else {
        send(Command::Version);
    }
}
void DeviceService::disconnectDevice() {
    bestEffortSafeShutdown();
    const bool motionWasActive = motionTimer_.isActive();
    const bool linearityWasActive = linearity_;
    const bool phaseAngleWasActive = phaseAngleTimer_.isActive();
    const bool angleErrorWasActive = angleErrorTimer_.isActive();
    handshakeTimer_.stop();
    handshakeQueryTimer_.stop();
    heartbeatTimer_.stop();
    responseWatchdog_.stop();
    motionTimer_.stop();
    encoderTimer_.stop();
    phaseAngleTimer_.stop();
    angleErrorTimer_.stop();
    pollTimer_.stop();
    inspectionStartTimer_.stop();
    inspectionStatusTimer_.stop();
    parameterTimer_.stop();
    parameterResponseTimer_.stop();
    firmwareTimer_.stop();
    firmwareTimeoutTimer_.stop();
    firmwarePostBootTimer_.stop();
    transport_->close();
    connectionState_ = ConnectionState::Disconnected;
    enabled_ = linearity_ = false;
    calibrationTarget_ = CalibrationTarget::None;
    target_ = {};
    hasTelemetry_ = false;
    operation_ = DeviceOperation::None;
    pendingParameterGroup_.reset();
    parameterQueue_.clear();
    pendingParameterPersist_ = false;
    parameterSavePending_ = false;
    safetyDisablePending_ = false;
    awaitingPostInspectionHeartbeat_ = false;
    motionResponsesSinceRateTick_ = 0;
    phaseAngleAwaitingResponse_ = angleErrorAwaitingResponse_ =
        canFdCountersAwaitingResponse_ = false;
    pendingCanFdDataRate_.reset();
    firmwareStage_ = FirmwareStage::Idle;
    firmwareBytes_.clear();
    firmwareOffset_ = 0;
    firmwareCrc_ = 0;
    emit stateChanged();
    if (motionWasActive)
        emit motionStateChanged(false);
    if (linearityWasActive)
        emit linearityStateChanged(false);
    if (phaseAngleWasActive)
        emit phaseAngleQueryStateChanged(false);
    if (angleErrorWasActive)
        emit angleErrorQueryStateChanged(false);
    emit versionChanged("—");
    emit refreshRateChanged(0);
}
void DeviceService::failConnection(QString reason) {
    if (connectionFailed() || connectionState_ == ConnectionState::Disconnected)
        return;
    bestEffortSafeShutdown();
    handshakeTimer_.stop();
    handshakeQueryTimer_.stop();
    heartbeatTimer_.stop();
    responseWatchdog_.stop();
    const bool motionWasActive = motionTimer_.isActive();
    const bool linearityWasActive = linearity_;
    const bool phaseAngleWasActive = phaseAngleTimer_.isActive();
    const bool angleErrorWasActive = angleErrorTimer_.isActive();
    motionTimer_.stop();
    encoderTimer_.stop();
    phaseAngleTimer_.stop();
    angleErrorTimer_.stop();
    pollTimer_.stop();
    inspectionStartTimer_.stop();
    inspectionStatusTimer_.stop();
    parameterTimer_.stop();
    parameterResponseTimer_.stop();
    firmwareTimer_.stop();
    firmwareTimeoutTimer_.stop();
    firmwarePostBootTimer_.stop();
    transport_->close();
    connectionState_ = ConnectionState::Failed;
    enabled_ = linearity_ = false;
    calibrationTarget_ = CalibrationTarget::None;
    target_ = {};
    hasTelemetry_ = false;
    operation_ = DeviceOperation::None;
    pendingParameterGroup_.reset();
    parameterQueue_.clear();
    pendingParameterPersist_ = false;
    parameterSavePending_ = false;
    safetyDisablePending_ = false;
    awaitingPostInspectionHeartbeat_ = false;
    motionResponsesSinceRateTick_ = 0;
    phaseAngleAwaitingResponse_ = angleErrorAwaitingResponse_ =
        canFdCountersAwaitingResponse_ = false;
    pendingCanFdDataRate_.reset();
    firmwareStage_ = FirmwareStage::Idle;
    firmwareBytes_.clear();
    firmwareOffset_ = 0;
    firmwareCrc_ = 0;
    emit targetChanged(target_);
    emit versionChanged("—");
    emit refreshRateChanged(0);
    emit stateChanged();
    if (motionWasActive)
        emit motionStateChanged(false);
    if (linearityWasActive)
        emit linearityStateChanged(false);
    if (phaseAngleWasActive)
        emit phaseAngleQueryStateChanged(false);
    if (angleErrorWasActive)
        emit angleErrorQueryStateChanged(false);
    emit message(std::move(reason));
}

// ---------------- 帧构造与独立控制 ----------------
CanFrame DeviceService::makeFrame(Command command, QByteArray payload) const {
    CanFrame frame{};
    frame.board = quint8(address_.ip.section('.', 3).toUInt());
    frame.channel = address_.channel;
    frame.motor = address_.motor;
    frame.command = code(command);
    frame.payload = std::move(payload);
    return frame;
}

void DeviceService::bestEffortSafeShutdown() {
    if (!connected() || !transport_->isOpen())
        return;
    // 这些都是协议中已经定义的普通帧。关闭会话时直接使用 Transport，
    // 避免尽力发送失败后递归触发已经处于关闭过程的会话失败。
    if (motionTimer_.isActive())
        transport_->send(makeFrame(Command::Motion, Codec::motion({})));
    if (enabled_ || safetyDisablePending_)
        transport_->send(makeFrame(Command::Disable));
}

void DeviceService::requestSafetyDisable(QString reason) {
    if (!connected() || !enabled_ || safetyDisablePending_)
        return;
    stopMotion();
    if (!connected())
        return;
    safetyDisablePending_ = true;
    if (!send(Command::Disable)) {
        safetyDisablePending_ = false;
        return;
    }
    emit message(std::move(reason));
}

bool DeviceService::send(Command c, QByteArray data) {
    const bool handshakeRequest = connecting() && c == Command::Version;
    if (!connected() && !handshakeRequest) {
        emit message("请先连接设备");
        return false;
    }
    const auto f = makeFrame(c, std::move(data));
    if (!transport_->send(f)) {
        if (connecting())
            failConnection("通信建立失败 · 版本请求发送失败");
        else
            emit message(QStringLiteral("UDP 发送失败 · 命令 %1").arg(code(c)));
        return false;
    }
    return true;
}
void DeviceService::setEnabled(bool value) {
    // 失能请求始终允许发送，这是操作人员退出不再产生完成数据的
    // 校准或检测流程的安全通道。
    if (value && busy() && operation_ != DeviceOperation::Calibration) {
        emit message("当前任务进行中，请先停止或等待结束");
        return;
    }
    const auto previousCalibration = calibrationTarget_;
    const bool releaseCalibration = value && operation_ == DeviceOperation::Calibration;
    if (releaseCalibration) {
        // 老上位机在校准请求后始终允许命令 2。
        // 此处只释放本地等待状态，协议中不存在未记录的取消帧。
        calibrationTarget_ = CalibrationTarget::None;
        setOperation(DeviceOperation::None);
    }
    if (!value) {
        stopMotion();
        stopLinearity();
        if (operation_ == DeviceOperation::Calibration)
            cancelCalibration();
        else if (operation_ == DeviceOperation::Inspection)
            stopInspection();
    }
    safetyDisablePending_ = !value;
    if (!send(value ? Command::Enable : Command::Disable)) {
        safetyDisablePending_ = false;
        if (releaseCalibration) {
            calibrationTarget_ = previousCalibration;
            setOperation(DeviceOperation::Calibration);
        }
    }
    if (value)
        clock_.start();
}
void DeviceService::command(Command c) {
    // 这些控件在已部署上位机中对应相互独立的协议命令。
    // 不得因为另一条命令正在运行而停止数据流或抑制该帧。
    send(c);
}

void DeviceService::sendMotionFrame(double timelineSeconds) {
    // 与 LoopSender 完全一致：振荡器使用从电机使能或清除曲线开始计时的
    // float 时间，并沿用已部署程序中的圆周率常量 3.1415926。
    constexpr double LegacyPi = 3.1415926;
    const float legacySeconds = float(timelineSeconds);
    const float frequency = float(program_.frequency);
    const float amplitude = float(program_.amplitude);
    const float offset = float(program_.offset);
    const double phase = 2.0 * LegacyPi * frequency * legacySeconds;
    if (program_.waveform == Waveform::Constant) {
        target_ = program_.target;
    } else if (program_.waveform == Waveform::PositionSine) {
        target_.position = float(amplitude * std::sin(phase) + offset);
        target_.velocity = float(2.0 * LegacyPi * frequency * amplitude * std::cos(phase));
        target_.torque = 0;
        target_.kp = program_.target.kp;
        target_.kd = program_.target.kd;
    } else {
        target_ = {};
        target_.torque = float(amplitude * std::sin(phase));
    }
    // 与已部署的循环发送器一致：命令 4 保持独立的 500 Hz 节拍，
    // 不等待上一帧应答。协议不含序号字段，因此不得用推测的请求、应答
    // 配对关系控制发送。
    send(Command::Motion, Codec::motion(target_));
    if (!connected())
        return;
    emit motionTargetSent(timelineSeconds, target_);
}

void DeviceService::startMotion(const MotionProgram& p) {
    if (!enabled_ || busy()) {
        emit message("需要先使能设备，并结束其他任务");
        return;
    }
    if (!std::isfinite(p.frequency) || p.frequency < 0 || p.frequency > 20 || !std::isfinite(p.amplitude) ||
        !std::isfinite(p.offset) || Codec::motion(p.target).isEmpty()) {
        emit message("运动参数无效");
        return;
    }
    const bool wasActive = motionTimer_.isActive();
    program_ = p;
    if (!wasActive) {
        // LoopSender 等待首个 2 ms 截止点后才发送命令 4；
        // 原位更新参数时保持现有节拍。
        motionTimer_.start();
        emit motionStateChanged(true);
    }
    emit message(wasActive ? "运动参数已立即更新" : "运动控制已启动");
}
void DeviceService::stopMotion() {
    const bool wasActive = motionTimer_.isActive();
    // 空闲电机的使能、失能不得凭空产生运动事务。
    // 全零 Motion 帧仅作为实际运行过的运动流终止命令，否则它的反馈会
    // 生成一条虚假的曲线数据。
    if (!wasActive)
        return;
    motionTimer_.stop();
    target_ = {};
    // 在发送终止用全零命令前先发布运行结束事件。
    // CSV 记录器等消费者可先关闭当前记录，确保停止命令的应答不会被
    // 当作实时运行数据写入。
    emit motionStateChanged(false);
    if (connected())
        send(Command::Motion, Codec::motion(target_));
    emit targetChanged(target_);
    emit message("运动指令已停止");
}

void DeviceService::resetMotionTimeline() {
    // 老上位机的清除曲线操作会重置共享起始时间，因此图表 X 轴与
    // 正在运行的正弦振荡器会同时从零重新开始。
    clock_.start();
}
void DeviceService::startLinearity() {
    if (!connected()) {
        emit message("请先连接设备");
        return;
    }
    if (!enabled_) {
        emit message("请先使能电机");
        return;
    }
    if (busy()) {
        emit message("当前任务进行中");
        return;
    }
    if (linearity_)
        return;
    linearity_ = true;
    // LinearityChecker 使用与 LoopSender 相同的 500 Hz 截止时间循环，
    // 仅在首个 2 ms 截止点到达时发送第一条命令。
    encoderTimer_.start();
    emit linearityStateChanged(true);
    emit message("线性度检查开始");
}
void DeviceService::stopLinearity() {
    if (!linearity_)
        return;
    linearity_ = false;
    encoderTimer_.stop();
    if (connected())
        send(Command::StopEncoder);
    emit linearityStateChanged(false);
    emit message("线性度检查停止");
}
void DeviceService::startPhaseAngleQuery() {
    if (!connected()) {
        emit message("请先连接设备");
        return;
    }
    if (phaseAngleTimer_.isActive())
        return;
    phaseAngleAwaitingResponse_ = true;
    phaseAngleTimer_.start();
    emit phaseAngleQueryStateChanged(true);
    send(Command::GetPhaseAngle);
    emit message("UVW 相位与电角度读取已启动");
}
void DeviceService::stopPhaseAngleQuery() {
    if (!phaseAngleTimer_.isActive())
        return;
    phaseAngleTimer_.stop();
    phaseAngleAwaitingResponse_ = false;
    emit phaseAngleQueryStateChanged(false);
    emit message("UVW 相位与电角度读取已停止");
}
void DeviceService::startAngleErrorQuery() {
    if (!connected()) {
        emit message("请先连接设备");
        return;
    }
    if (angleErrorTimer_.isActive())
        return;
    angleErrorAwaitingResponse_ = true;
    angleErrorTimer_.start();
    emit angleErrorQueryStateChanged(true);
    send(Command::GetAngleError);
    emit message("角度误差读取已启动");
}
void DeviceService::stopAngleErrorQuery() {
    if (!angleErrorTimer_.isActive())
        return;
    angleErrorTimer_.stop();
    angleErrorAwaitingResponse_ = false;
    emit angleErrorQueryStateChanged(false);
    emit message("角度误差读取已停止");
}
void DeviceService::queryCanFdCounters() {
    if (!connected()) {
        emit message(QStringLiteral("请先连接设备"));
        return;
    }
    canFdCountersAwaitingResponse_ = true;
    send(Command::GetCanFdTxRxCount);
}
void DeviceService::setCanFdDataRate(quint8 rate) {
    if (!connected() || enabled_ || busy()) {
        emit message(QStringLiteral("修改CAN FD数据波特率需要已连接、已失能且无其他任务"));
        emit canFdDataRateRequestSent(rate, false);
        return;
    }
    if (rate != 5 && rate != 8) {
        emit message(QStringLiteral("CAN FD数据波特率只支持5 Mbps或8 Mbps"));
        emit canFdDataRateRequestSent(rate, false);
        return;
    }
    pendingCanFdDataRate_ = rate;
    const bool sent = send(Command::SetCanFdDataRate, QByteArray(1, char(rate)));
    emit canFdDataRateRequestSent(rate, sent);
    if (sent) {
        emit message(QStringLiteral("命令37已发送：DLC 1，Byte0=%1 Mbps；等待设备应答")
                         .arg(rate));
    } else {
        pendingCanFdDataRate_.reset();
    }
}

// ---------------- 校准与下线检测 ----------------
void DeviceService::calibrate(bool output) {
    const bool replacingCalibration = operation_ == DeviceOperation::Calibration;
    if (!connected() || enabled_ || (busy() && !replacingCalibration)) {
        emit message("校准前请先失能，并结束其他任务");
        return;
    }
    stopMotion();
    const auto previousTarget = calibrationTarget_;
    calibrationTarget_ = output ? CalibrationTarget::Output : CalibrationTarget::Input;
    if (!replacingCalibration)
        setOperation(DeviceOperation::Calibration);
    if (!send(output ? Command::CalibrateOutput : Command::CalibrateInput)) {
        calibrationTarget_ = previousTarget;
        if (!replacingCalibration)
            setOperation(DeviceOperation::None);
    }
}
void DeviceService::cancelCalibration() {
    if (operation_ != DeviceOperation::Calibration) {
        emit message(QStringLiteral("当前没有正在等待的校准任务"));
        return;
    }
    calibrationTarget_ = CalibrationTarget::None;
    setOperation(DeviceOperation::None);
    emit message(QStringLiteral("已结束校准等待"));
}
void DeviceService::startInspection(const std::array<float, 4>& scope) {
    if (!connected() || enabled_ || busy()) {
        emit message("下线检查前请先失能，并结束其他任务");
        return;
    }
    for (auto v : scope)
        if (!std::isfinite(v)) {
            emit message("测试阈值无效");
            return;
        }
    stopMotion();
    stopLinearity();
    inspectionStep_ = 0;
    inspectionFailed_ = false;
    inspectionFailureText_.clear();
    setOperation(DeviceOperation::Inspection);
    if (!send(Command::TestScope, Codec::floats({scope[0], scope[1], scope[2], scope[3]})))
        setOperation(DeviceOperation::None);
}
void DeviceService::stopInspection() {
    const bool wasInspection = operation_ == DeviceOperation::Inspection;
    pollTimer_.stop();
    inspectionStartTimer_.stop();
    inspectionStatusTimer_.stop();
    if (wasInspection)
        setOperation(DeviceOperation::None);
    if (wasInspection && connected()) {
        // 命令 33 启动驱动器侧流程，协议未定义停止命令。
        // 驱动器仍可能在完成该流程时，不得将缺少空闲或版本应答解释为断链。
        awaitingPostInspectionHeartbeat_ = true;
        responseWatchdog_.start();
    }
    inspectionStep_ = 0;
    inspectionFailed_ = false;
    inspectionFailureText_.clear();
    emit message("检查已停止");
}

// ---------------- 参数与固件升级 ----------------
void DeviceService::readParameters() {
    if (!connected()) {
        emit message("请先连接设备");
        return;
    }
    if (!busy())
        send(Command::Config);
}
void DeviceService::writeParameters(const QList<double>& v, ParameterGroup group, bool persist) {
    if (v.size() != 9 || !connected() || enabled_ || busy()) {
        emit message("参数写入需要已连接、已失能且无其他任务");
        return;
    }
    if (group == ParameterGroup::Motor) {
        const int targetMotorId = int(v[1]);
        if (targetMotorId < 0 || targetMotorId > 15) {
            emit message("电机 ID / 关节 ID 必须在 0 到 15 之间");
            return;
        }
    }
    parameterQueue_.clear();
    pendingParameterGroup_ = group;
    pendingParameterPersist_ = persist;
    setOperation(DeviceOperation::Parameters);
    if (group == ParameterGroup::Motor) {
        const int targetMotorId = int(v[1]);
        parameterQueue_.append({Command::SetGear, Codec::floats({float(v[0])})});
        parameterQueue_.append({Command::SetBandwidth, Codec::u16(quint16(v[3]))});
        parameterQueue_.append({Command::SetTimeout, QByteArray(1, char(int(v[2])))});
        parameterQueue_.append({Command::SetId, QByteArray(1, char(targetMotorId))});
    } else {
        parameterQueue_.append(
            {Command::SetUnderVoltage, Codec::u16(quint16(v[5] * 10))});
        parameterQueue_.append(
            {Command::SetOverVoltage, Codec::u16(quint16(v[6] * 10))});
        parameterQueue_.append({Command::SetCurrent, Codec::u16(quint16(v[4] * 10))});
        parameterQueue_.append(
            {Command::SetMotorTemperature, Codec::u16(quint16(v[7] * 10))});
        parameterQueue_.append(
            {Command::SetDriverTemperature, Codec::u16(quint16(v[8] * 10))});
    }
    if (persist)
        parameterQueue_.append({Command::SaveConfig, {}});
    sendNextParameterCommand();
}
void DeviceService::sendNextParameterCommand() {
    if (!pendingParameterGroup_ || parameterQueue_.isEmpty()) {
        finishParameterWrite();
        return;
    }
    const auto item = parameterQueue_.takeFirst();
    if (item.first == Command::SaveConfig)
        parameterSavePending_ = true;
    if (!send(item.first, item.second)) {
        failParameterWrite(QStringLiteral("参数写入失败 · 命令 %1 发送失败")
                               .arg(code(item.first)));
        return;
    }
    if (item.first == Command::SaveConfig) {
        // UDP 写入成功只代表请求到达本地套接字。
        // 必须等命令 16 返回零后才能报告参数已保存。
        if (parameterSavePending_)
            parameterResponseTimer_.start();
        return;
    }
    // 已部署程序中每条参数命令间隔 100 ms。
    // SET_ID 不会在本次事务中改写当前会话地址。
    if (connected())
        parameterTimer_.start();
}
void DeviceService::finishParameterWrite() {
    if (!pendingParameterGroup_)
        return;
    const auto group = *pendingParameterGroup_;
    const bool persist = pendingParameterPersist_;
    pendingParameterGroup_.reset();
    pendingParameterPersist_ = false;
    parameterSavePending_ = false;
    parameterResponseTimer_.stop();
    parameterQueue_.clear();
    setOperation(DeviceOperation::None);
    emit stateChanged();
    const auto name = group == ParameterGroup::Motor ? "电机参数" : "保护参数";
    emit message(persist ? QString("%1已写入设备").arg(name)
                         : QString("%1命令已发送").arg(name));
}
void DeviceService::failParameterWrite(QString reason) {
    parameterTimer_.stop();
    parameterResponseTimer_.stop();
    parameterQueue_.clear();
    pendingParameterGroup_.reset();
    pendingParameterPersist_ = false;
    parameterSavePending_ = false;
    if (operation_ == DeviceOperation::Parameters)
        setOperation(DeviceOperation::None);
    emit message(std::move(reason));
}

void DeviceService::startFirmwareUpdate(const QString& filePath) {
    if (!connected() || enabled_ || busy()) {
        emit message("固件升级需要已连接、已失能且无其他任务");
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        emit message("固件升级失败 · 无法打开固件文件");
        return;
    }
    // QByteArray 与老协议的 uint32 长度字段都有有限范围。
    // 对无法表示的镜像直接拒绝，不能截断线上传输长度。
    const qint64 maximumSize = qMin<qint64>(std::numeric_limits<int>::max() - 7,
                                           std::numeric_limits<quint32>::max() - 7);
    if (file.size() <= 0 || file.size() > maximumSize) {
        emit message(file.size() <= 0 ? "固件升级失败 · 固件文件为空"
                                      : "固件升级失败 · 固件文件过大");
        return;
    }
    firmwareBytes_ = file.readAll();
    if (firmwareBytes_.size() != file.size()) {
        firmwareBytes_.clear();
        emit message("固件升级失败 · 读取固件文件不完整");
        return;
    }

    const int padding = int((8 - firmwareBytes_.size() % 8) % 8);
    if (padding)
        firmwareBytes_.append(QByteArray(padding, 0));
    firmwareOffset_ = 0;
    firmwareCrc_ = Codec::crc32(firmwareBytes_);
    firmwarePostBootTimer_.stop();
    stopMotion();
    stopLinearity();
    setOperation(DeviceOperation::Firmware);
    firmwareStage_ = FirmwareStage::WaitingErase;
    emit firmwareProgress(0);
    emit firmwareStatusChanged("正在请求设备擦除升级区");
    emit message("固件升级开始");

    // 发送前先设置状态和超时。回环或高速适配器可能在
    // Transport::send() 内同步送回应答。
    firmwareTimeoutTimer_.start();
    if (!send(Command::FirmwareStart))
        failFirmwareUpdate(QStringLiteral("固件升级失败 · 命令 18 发送失败"));
}

void DeviceService::sendNextFirmwareBlock() {
    if (!connected())
        return;
    if (firmwareStage_ == FirmwareStage::WaitingFinalGap) {
        firmwareStage_ = FirmwareStage::WaitingCheck;
        emit firmwareStatusChanged("固件传输完成，正在校验");
        firmwareTimeoutTimer_.start();
        if (!send(Command::FirmwareCheck,
                  Codec::u32(quint32(firmwareBytes_.size())) + Codec::u32(firmwareCrc_)))
            failFirmwareUpdate(QStringLiteral("固件升级失败 · 命令 20 发送失败"));
        return;
    }
    if (firmwareStage_ != FirmwareStage::Writing)
        return;
    if (firmwareOffset_ >= firmwareBytes_.size()) {
        // 老线程在最后一个数据块之后休眠一次，退出文件循环后又休眠一次；
        // 这里保留两次 2 ms 间隔。
        firmwareStage_ = FirmwareStage::WaitingFinalGap;
        firmwareTimer_.start();
        return;
    }

    const QByteArray block = firmwareBytes_.mid(firmwareOffset_, 8);
    if (!send(Command::FirmwareWrite, block)) {
        failFirmwareUpdate(QStringLiteral("固件升级失败 · 命令 19 发送失败"));
        return;
    }
    firmwareOffset_ += 8;
    const int progress = 1 + int((98 * firmwareOffset_) / firmwareBytes_.size());
    emit firmwareProgress(qMin(progress, 99));
    firmwareTimer_.start();
}

void DeviceService::failFirmwareUpdate(QString reason) {
    firmwareTimer_.stop();
    firmwareTimeoutTimer_.stop();
    firmwarePostBootTimer_.stop();
    firmwareStage_ = FirmwareStage::Idle;
    firmwareBytes_.clear();
    firmwareOffset_ = 0;
    firmwareCrc_ = 0;
    if (operation_ == DeviceOperation::Firmware)
        setOperation(DeviceOperation::None);
    emit firmwareStatusChanged(reason);
    emit message(std::move(reason));
}

// ---------------- 单一接收侧命令路由 ----------------
void DeviceService::receive(const CanFrame& f) {
    if ((!connected() && !connecting()) || f.board != address_.ip.section('.', 3).toUInt() ||
        f.channel != address_.channel || f.motor != address_.motor)
        return;
    if (connecting() && f.command != code(Command::Version))
        return;
    if (connected())
        responseWatchdog_.start();
    CanFrame diagnosticFrame = f;
    diagnosticFrame.receivedAt = QDateTime::currentDateTime();
    emit responseFrame(diagnosticFrame);
    const auto& p = f.payload;
    switch (static_cast<Command>(f.command)) {
    case Command::Motion:
        if (auto t = Codec::telemetry(p)) {
            ++motionResponsesSinceRateTick_;
            const double receivedSeconds = clock_.nsecsElapsed() / 1.0e9;
            t->seconds = receivedSeconds;
            lastTelemetry_ = *t;
            hasTelemetry_ = true;
            emit telemetryReceived(*t);
            // 已部署上位机在同一主机时间线上绘制收到的反馈值和当前目标值。
            // 不得为命令 4 虚构请求标识。
            if (motionTimer_.isActive())
                emit motionSampleReceived(*t, receivedSeconds, target_);
        }
        break;
    case Command::Enable:
        if (p.size() >= 2) {
            const auto result = Codec::readU16(p, 0);
            if (result == 0) {
                safetyDisablePending_ = false;
                enabled_ = true;
                emit stateChanged();
                emit message("电机使能成功");
            } else {
                emit message(QString("电机使能失败 · 错误码 %1").arg(result));
                requestSafetyDisable(QStringLiteral("电机使能失败 · 已发送电机失能命令"));
            }
        }
        break;
    case Command::Disable:
        if (!p.isEmpty()) {
            if (p[0] == 0) {
                safetyDisablePending_ = false;
                enabled_ = false;
                emit stateChanged();
                emit message("电机失能成功");
            } else {
                safetyDisablePending_ = false;
                emit message("电机失能失败");
            }
        }
        break;
    case Command::Reset:
        if (!p.isEmpty())
            emit message(p[0] == 0 ? "电机复位成功" : "电机复位失败");
        break;
    case Command::SetHome:
    case Command::ClearError:
        if (!p.isEmpty()) {
            const auto command = static_cast<Command>(f.command);
            const bool ok = p[0] == 0;
            emit message(command == Command::SetHome
                             ? (ok ? "设置零点成功" : "设置零点失败")
                             : (ok ? "清除错误成功" : "清除错误失败"));
        }
        break;
    case Command::StopEncoder:
        if (!p.isEmpty() && p[0] == 0)
            emit message("编码器采集已停止");
        break;
    case Command::SaveConfig:
        if (parameterSavePending_ && !p.isEmpty()) {
            parameterResponseTimer_.stop();
            if (quint8(p[0]) == 0)
                finishParameterWrite();
            else
                failParameterWrite(QStringLiteral("参数写入失败 · 命令 16 返回码 %1")
                                       .arg(quint8(p[0])));
        }
        break;
    case Command::FirmwareStart:
        if (operation_ == DeviceOperation::Firmware && firmwareStage_ == FirmwareStage::WaitingErase &&
            !p.isEmpty()) {
            firmwareTimeoutTimer_.stop();
            if (quint8(p[0]) != 0) {
                failFirmwareUpdate("固件升级失败 · 设备擦除升级区失败");
                break;
            }
            firmwareStage_ = FirmwareStage::Writing;
            emit firmwareProgress(1);
            emit firmwareStatusChanged("升级区已擦除，正在传输固件");
            sendNextFirmwareBlock();
        }
        break;
    case Command::FirmwareWrite:
        // 已部署升级程序有意不等待逐块确认。
        // 保持固定 500 Hz 数据流，由命令 20 校验全部字节。
        break;
    case Command::FirmwareCheck:
        if (operation_ == DeviceOperation::Firmware && firmwareStage_ == FirmwareStage::WaitingCheck &&
            !p.isEmpty()) {
            firmwareTimeoutTimer_.stop();
            const quint8 result = quint8(p[0]);
            if (result != 0) {
                failFirmwareUpdate(result == 1 ? "固件升级失败 · 设备报告长度不一致"
                                               : result == 2 ? "固件升级失败 · 设备报告 CRC 校验失败"
                                                             : QString("固件升级失败 · 校验错误码 %1").arg(result));
                break;
            }
            if (!send(Command::FirmwareBoot)) {
                failFirmwareUpdate(QStringLiteral("固件升级失败 · 命令 21 发送失败"));
                break;
            }
            firmwareStage_ = FirmwareStage::WaitingRebootQuery;
            firmwareBytes_.clear();
            firmwareOffset_ = 0;
            firmwareCrc_ = 0;
            emit firmwareProgress(100);
            emit firmwareStatusChanged("固件升级完成，正在查询新版本");
            emit message("固件升级完成");
            firmwarePostBootTimer_.start();
        }
        break;
    case Command::FirmwareBoot:
        break;
    case Command::Version:
        if (p.size() >= 2) {
            awaitingPostInspectionHeartbeat_ = false;
            // 已部署头文件声明应答为两字节，但解析器还会读取字节 2..3。
            // 保持该兼容行为：设备只返回两字节时，后缀按零处理。
            const quint16 suffix = p.size() >= 4 ? Codec::readU16(p, 2) : 0;
            emit versionChanged(QString("V%1-%2").arg(Codec::readU16(p, 0)).arg(suffix));
            if (connecting()) {
                handshakeTimer_.stop();
                handshakeQueryTimer_.stop();
                connectionState_ = ConnectionState::Connected;
                heartbeatTimer_.start();
                responseWatchdog_.start();
                emit stateChanged();
                emit message("通信建立成功 · " + address_.key());
            }
        }
        break;
    case Command::Encoder:
        if (linearity_ && p.size() >= 4)
            emit encoderPoint(clock_.nsecsElapsed() / 1.0e9, Codec::readFloat(p, 0));
        break;
    case Command::GetPhaseAngle:
        if (phaseAngleTimer_.isActive() && p.size() >= 8) {
            const PhaseAngleData data{Codec::readI32(p, 0), Codec::readFloat(p, 4)};
            if (std::isfinite(data.electricalAngleOffset)) {
                emit phaseAngleReceived(data);
                if (phaseAngleAwaitingResponse_) {
                    phaseAngleAwaitingResponse_ = false;
                    emit message(QStringLiteral("UVW相位与电角度已收到设备应答"));
                }
            }
        }
        break;
    case Command::GetAngleError:
        if (angleErrorTimer_.isActive() && p.size() >= 16) {
            const AngleErrorData data{Codec::readFloat(p, 0), Codec::readFloat(p, 4),
                                      Codec::readFloat(p, 8), Codec::readFloat(p, 12)};
            if (std::isfinite(data.referenceAngle) && std::isfinite(data.jointAngle) &&
                std::isfinite(data.errorRadians) && std::isfinite(data.errorDegrees)) {
                emit angleErrorReceived(data);
                if (angleErrorAwaitingResponse_) {
                    angleErrorAwaitingResponse_ = false;
                    emit message(QStringLiteral("角度误差读取已收到设备应答"));
                }
            }
        }
        break;
    case Command::SetCanFdDataRate:
        if (!p.isEmpty()) {
            const quint8 actual = quint8(p[0]);
            const quint8 requested = pendingCanFdDataRate_.value_or(0);
            const bool success = pendingCanFdDataRate_.has_value() && actual == requested &&
                                 (actual == 5 || actual == 8);
            pendingCanFdDataRate_.reset();
            emit canFdDataRateResponse(requested, actual, success);
            emit message(success
                             ? QStringLiteral("已收到命令37设备应答：DLC 1，Byte0=%1 Mbps")
                                   .arg(actual)
                             : QStringLiteral("已收到命令37设备应答，但数据不匹配：请求%1 Mbps，应答%2 Mbps")
                                   .arg(requested)
                                   .arg(actual));
        }
        break;
    case Command::GetCanFdTxRxCount:
        if (p.size() >= 8) {
            const auto readU32 = [&p](int offset) {
                return quint32(Codec::readU16(p, offset)) |
                       (quint32(Codec::readU16(p, offset + 2)) << 16);
            };
            emit canFdCountersReceived({readU32(0), readU32(4)});
            if (canFdCountersAwaitingResponse_) {
                canFdCountersAwaitingResponse_ = false;
                emit message(QStringLiteral("CAN FD收发计数查询已收到设备应答"));
            }
        }
        break;
    case Command::CalibrateInput:
        if (p.size() >= 2) {
            const quint16 result = Codec::readU16(p, 0);
            emit inputCalibrationCommandResponse(result);
            if (operation_ == DeviceOperation::Calibration &&
                calibrationTarget_ == CalibrationTarget::Input) {
                if (result != 0) {
                    setOperation(DeviceOperation::None);
                    calibrationTarget_ = CalibrationTarget::None;
                    const QString text = QString("输入轴校准失败 · 错误码 %1").arg(result);
                    emit message(text);
                }
            }
        }
        break;
    case Command::CalibrateOutput:
        if (p.size() >= 2) {
            const quint16 result = Codec::readU16(p, 0);
            emit outputCalibrationCommandResponse(result);
            if (operation_ != DeviceOperation::Calibration ||
                calibrationTarget_ != CalibrationTarget::Output)
                break;
            if (result == 0) {
                emit message("输出端编码器校准中");
            } else {
                setOperation(DeviceOperation::None);
                calibrationTarget_ = CalibrationTarget::None;
                const QString text = QString("输出轴校准失败 · 错误码 %1").arg(result);
                emit message(text);
            }
        }
        break;
    case Command::CalibrationReport:
        if (p.size() < 8)
            break;
        {
            const quint16 step = Codec::readU16(p, 0);
            const quint16 data0 = Codec::readU16(p, 2);
            const quint16 data1 = Codec::readU16(p, 4);
            const quint16 data2 = Codec::readU16(p, 6);
            switch (step) {
            case 0:
                emit inputCalibrationReportReceived(step, data0, data1, data2);
                break;
            case 1: {
                emit inputCalibrationReportReceived(step, data0, data1, data2);
                break;
            }
            case 2:
                emit inputCalibrationPoint(0, Codec::readFloat(p, 4));
                break;
            case 3:
                emit inputCalibrationPoint(1, qint32(quint32(data1) | (quint32(data2) << 16)));
                break;
            case 4:
                // 与老上位机一致：字节 2..5 包含电气零位，
                // 但该阶段不向用户显示通知。
                break;
            case 5: {
                const bool activeInput = operation_ == DeviceOperation::Calibration &&
                                         calibrationTarget_ == CalibrationTarget::Input;
                if (activeInput) {
                    setOperation(DeviceOperation::None);
                    calibrationTarget_ = CalibrationTarget::None;
                }
                // 在成功通知前先发布空闲状态，使界面立即恢复使能和校准控件。
                emit inputCalibrationReportReceived(step, data0, data1, data2);
                if (activeInput)
                    emit message("输入轴校准完成");
                break;
            }
            case 6: {
                const bool activeOutput = operation_ == DeviceOperation::Calibration &&
                                          calibrationTarget_ == CalibrationTarget::Output;
                const bool success = data0 == 0 && data1 == 0 && data2 == 0;
                if (activeOutput) {
                    setOperation(DeviceOperation::None);
                    calibrationTarget_ = CalibrationTarget::None;
                }
                emit outputCalibrationResultReceived(data0, data1, data2);
                if (!activeOutput)
                    break;
                const QString text =
                    success
                        ? QStringLiteral("输出轴校准成功 · step 6")
                        : QStringLiteral(
                              "输出轴校准未成功 · step 6 · DATA0=%1 · DATA1=%2 · DATA2=%3")
                              .arg(data0)
                              .arg(data1)
                              .arg(data2);
                emit message(text);
                break;
            }
            default:
                break;
            }
        }
        break;
    case Command::TestScope:
        if (operation_ != DeviceOperation::Inspection || p.isEmpty())
            break;
        if (p[0] == 1) {
            emit inspectionStatusChanged("设置成功");
            // 命令 32 被接受后，老检测程序立即开始轮询命令 23，
            // 同时命令 33 继续保持 500 ms 间隔。
            inspectionStatusTimer_.start();
            inspectionStartTimer_.start();
            send(Command::TestStart);
        } else {
            emit inspectionStatusChanged("设置失败");
            setOperation(DeviceOperation::None);
        }
        break;
    case Command::TestStart:
        if (operation_ != DeviceOperation::Inspection || p.isEmpty())
            break;
        if (p[0] == 1) {
            inspectionStartTimer_.stop();
            emit inspectionStatusChanged("开始测试");
            QTimer::singleShot(500, this, [this] {
                if (operation_ != DeviceOperation::Inspection)
                    return;
                send(Command::TestStep);
                pollTimer_.start();
                QTimer::singleShot(500, this, [this] {
                    if (operation_ == DeviceOperation::Inspection)
                        send(Command::TestData);
                });
            });
        } else {
            inspectionStartTimer_.stop();
            inspectionStatusTimer_.stop();
            emit inspectionStatusChanged("测试失败");
            setOperation(DeviceOperation::None);
        }
        break;
    case Command::TestStep:
        if (operation_ == DeviceOperation::Inspection && !p.isEmpty()) {
            inspectionStep_ = quint8(p[0]);
            emit inspectionStep(inspectionStep_);
        }
        break;
    case Command::TestData:
        if (operation_ == DeviceOperation::Inspection && p.size() >= 32) {
            TestResult r;
            for (int i = 0; i < 8; ++i)
                r.values[i] = Codec::readFloat(p, i * 4);
            emit inspectionDataReceived(r);
            if (inspectionFailed_ || inspectionStep_ == 7) {
                r.result = inspectionFailed_ ? inspectionFailureText_ : QStringLiteral("测试成功");
                pollTimer_.stop();
                inspectionStartTimer_.stop();
                inspectionStatusTimer_.stop();
                setOperation(DeviceOperation::None);
                emit inspectionFinished(r);
            }
        }
        break;
    case Command::Status:
        if (p.size() >= 2) {
            const quint16 word = Codec::readU16(p, 0);
            const auto errors = legacyInspectionErrors(word);
            const QString text = errors.isEmpty() ? QStringLiteral("状态正常")
                                                  : QStringLiteral("异常: ") + errors.join(QStringLiteral("; "));
            emit statusWordReceived(word);
            emit inspectionStatusWordChanged(word, text);
            if (operation_ == DeviceOperation::Inspection && !errors.isEmpty()) {
                inspectionFailed_ = true;
                inspectionFailureText_ = errors.join(QStringLiteral("; "));
            }
            if (word != 0)
                requestSafetyDisable(QStringLiteral("设备报告故障 · 已发送电机失能命令"));
        }
        break;
    case Command::Config:
        if (p.size() >= 18) {
            QList<double> v{Codec::readFloat(p, 0), double(quint8(p[4])), double(quint8(p[5])),
                            double(Codec::readU16(p, 6))};
            for (int n = 8; n < 18; n += 2)
                v.append(Codec::readU16(p, n) / 10.);
            emit parametersReceived(v);
        }
        break;
    default:
        break;
    }
}
} // 命名空间 studio
