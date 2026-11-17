#pragma once
#include "domain/Types.h"
#include "transport/Transport.h"
#include <QElapsedTimer>
#include <QTimer>
#include <memory>
#include <optional>

namespace studio {
// 面向应用的状态机只依赖 Transport 接口，不依赖套接字或具体模拟器。
class DeviceService final : public QObject {
    Q_OBJECT
  public:
    enum class ConnectionState { Disconnected, Connecting, Connected, Failed };

    explicit DeviceService(QObject* parent = nullptr);
    DeviceService(std::unique_ptr<Transport> transport, QObject* parent);

    // 提供给界面门面的只读会话与任务状态。
    ConnectionState connectionState() const { return connectionState_; }
    bool connected() const { return connectionState_ == ConnectionState::Connected; }
    bool connecting() const { return connectionState_ == ConnectionState::Connecting; }
    bool connectionFailed() const { return connectionState_ == ConnectionState::Failed; }
    bool enabled() const { return enabled_; }
    bool motionActive() const { return motionTimer_.isActive(); }
    bool linearityActive() const { return linearity_; }
    bool phaseAngleQueryActive() const { return phaseAngleTimer_.isActive(); }
    bool angleErrorQueryActive() const { return angleErrorTimer_.isActive(); }
    int phaseAngleQueryHz() const { return phaseAngleQueryHz_; }
    int angleErrorQueryHz() const { return angleErrorQueryHz_; }
    bool busy() const { return operation_ != DeviceOperation::None; }
    DeviceOperation operation() const { return operation_; }
    DeviceAddress address() const { return address_; }
    DeviceSession session() const { return session_; }
    Motion target() const { return target_; }

    // 会话生命周期与独立设备命令。
    void connectDevice(DeviceAddress);
    void connectSession(DeviceSession);
    void disconnectDevice();
    void setEnabled(bool);
    void command(Command);
    void startMotion(const MotionProgram&);
    void stopMotion();
    void resetMotionTimeline();
    void startLinearity();
    void stopLinearity();
    void startPhaseAngleQuery();
    void stopPhaseAngleQuery();
    void startAngleErrorQuery();
    void stopAngleErrorQuery();
    void queryCanFdCounters();
    void setCanFdDataRate(quint8 megabitsPerSecond);

    // 多帧任务的互斥只属于主机状态，不改变命令码和独立应答解析。
    void calibrate(bool output);
    void cancelCalibration();
    void startInspection(const std::array<float, 4>&);
    void stopInspection();
    void readParameters();
    void writeParameters(const QList<double>&, ParameterGroup, bool persist);
    void startFirmwareUpdate(const QString& filePath);
  signals:
    // 会话与诊断信号。
    void stateChanged();
    // 仅在通过当前板卡、通道、电机过滤后发出，诊断页与业务使用同一应答。
    void responseFrame(studio::CanFrame frame);
    void telemetryReceived(studio::Telemetry);
    // 最近一秒内有效命令 4 遥测应答数，与旧程序的刷新率计数一致。
    void refreshRateChanged(int repliesPerSecond);

    // 运动与周期采集信号；反馈和当前目标共用主机时间轴。
    // 协议没有序号字段，因此不虚构请求与应答的一一对应关系。
    void motionSampleReceived(studio::Telemetry feedback, double sampleSeconds,
                              studio::Motion target);
    void targetChanged(studio::Motion);
    void motionTargetSent(double seconds, studio::Motion target);
    void motionStateChanged(bool active);
    void encoderPoint(double time, double value);
    void linearityStateChanged(bool active);
    void phaseAngleReceived(studio::PhaseAngleData data);
    void phaseAngleQueryStateChanged(bool active);
    void angleErrorReceived(studio::AngleErrorData data);
    void angleErrorQueryStateChanged(bool active);
    void canFdCountersReceived(studio::CanFdCounters counters);
    void canFdDataRateRequestSent(quint8 requested, bool success);
    // 仅在当前设备收到有效命令 37 应答后发出。
    void canFdDataRateResponse(quint8 requested, quint8 actual, bool success);

    // 校准与下线检测信号；输入轴和输出轴使用各自独立的应答信号。
    // 输入轴对应命令 3 与命令 31 的步骤 0～5；输出轴对应命令 25 与步骤 6。
    void inputCalibrationCommandResponse(quint16 result);
    void inputCalibrationReportReceived(quint16 step, quint16 data0, quint16 data1, quint16 data2);
    void inputCalibrationPoint(int series, double value);
    void outputCalibrationCommandResponse(quint16 result);
    void outputCalibrationResultReceived(quint16 data0, quint16 data1, quint16 data2);
    void inspectionStatusChanged(QString status);
    void inspectionStatusWordChanged(quint16 word, QString text);
    // 仅在当前设备收到有效命令 23 时发出；本地占位状态不能触发此信号。
    void statusWordReceived(quint16 word);
    void inspectionStep(int step);
    void inspectionDataReceived(studio::TestResult result);
    void inspectionFinished(studio::TestResult result);

    // 参数、固件与通用显示事件。
    void parametersReceived(QList<double> values);
    void firmwareProgress(int percent);
    void firmwareStatusChanged(QString status);
    void versionChanged(QString version);
    void message(QString text);

  private:
    // 帧构造与唯一的上行命令分派入口。
    bool send(Command, QByteArray payload = {});
    CanFrame makeFrame(Command, QByteArray payload = {}) const;
    void receive(const CanFrame&);
    void failConnection(QString reason);
    void bestEffortSafeShutdown();
    void requestSafetyDisable(QString reason);

    // 任务流程辅助函数。
    void sendMotionFrame(double timelineSeconds);
    void sendNextParameterCommand();
    void finishParameterWrite();
    void failParameterWrite(QString reason);
    void sendNextFirmwareBlock();
    void failFirmwareUpdate(QString reason);
    void setOperation(DeviceOperation);

    // 传输与会话的所有权。
    std::unique_ptr<Transport> transport_;
    DeviceAddress address_;
    DeviceSession session_;
    QElapsedTimer clock_;
    QTimer motionTimer_, encoderTimer_, phaseAngleTimer_, angleErrorTimer_, pollTimer_,
        inspectionStartTimer_, inspectionStatusTimer_, parameterTimer_, parameterResponseTimer_, firmwareTimer_,
        firmwareTimeoutTimer_, firmwarePostBootTimer_, handshakeTimer_, handshakeQueryTimer_, heartbeatTimer_,
        responseWatchdog_, refreshRateTimer_;
    MotionProgram program_;
    Motion target_;
    Telemetry lastTelemetry_;
    bool hasTelemetry_ = false;
    ConnectionState connectionState_ = ConnectionState::Disconnected;
    bool enabled_ = false, linearity_ = false;

    // 采集与任务状态；校准目标只记录当前操作，不影响各命令独立解析。
    enum class CalibrationTarget { None, Input, Output };
    CalibrationTarget calibrationTarget_ = CalibrationTarget::None;
    bool phaseAngleAwaitingResponse_ = false, angleErrorAwaitingResponse_ = false,
         canFdCountersAwaitingResponse_ = false;
    int phaseAngleQueryHz_ = 100, angleErrorQueryHz_ = 100;
    int motionResponsesSinceRateTick_ = 0;
    DeviceOperation operation_ = DeviceOperation::None;
    int inspectionStep_ = 0;
    bool inspectionFailed_ = false;
    QString inspectionFailureText_;
    std::optional<ParameterGroup> pendingParameterGroup_;
    QList<QPair<Command, QByteArray>> parameterQueue_;
    bool pendingParameterPersist_ = false;
    bool parameterSavePending_ = false;
    std::optional<quint8> pendingCanFdDataRate_;
    bool safetyDisablePending_ = false;
    // 旧协议没有驱动器端的下线检测停止命令。主机停止轮询后，须等待有效版本应答
    // 证明设备回到空闲命令集，再恢复普通连接超时判断。
    bool awaitingPostInspectionHeartbeat_ = false;
    enum class FirmwareStage {
        Idle,
        WaitingErase,
        Writing,
        WaitingFinalGap,
        WaitingCheck,
        WaitingRebootQuery
    };
    FirmwareStage firmwareStage_ = FirmwareStage::Idle;
    QByteArray firmwareBytes_;
    qsizetype firmwareOffset_ = 0;
    quint32 firmwareCrc_ = 0;
};
} // 命名空间 studio
