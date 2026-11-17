#pragma once

#include "services/DeviceService.h"
#include "transport/TransportFactory.h"
#include <QThread>
#include <functional>

namespace studio {

// 面向界面的 DeviceService 门面。设备服务、协议定时器、传输与解析都在 ioThread_ 中运行；
// 界面命令通过队列提交，绘图和诊断表格不会阻塞 CAN FD 收发。
class AsyncDeviceService final : public QObject {
    Q_OBJECT
  public:
    using ConnectionState = DeviceService::ConnectionState;

    explicit AsyncDeviceService(TransportKind kind, QObject* parent = nullptr);
    ~AsyncDeviceService() override;

    // 仅在界面线程更新的状态快照。
    ConnectionState connectionState() const { return connectionState_; }
    bool connected() const { return connectionState_ == ConnectionState::Connected; }
    bool connecting() const { return connectionState_ == ConnectionState::Connecting; }
    bool connectionFailed() const { return connectionState_ == ConnectionState::Failed; }
    bool enabled() const { return enabled_; }
    bool motionActive() const { return motionActive_; }
    bool linearityActive() const { return linearityActive_; }
    bool phaseAngleQueryActive() const { return phaseAngleQueryActive_; }
    bool angleErrorQueryActive() const { return angleErrorQueryActive_; }
    int phaseAngleQueryHz() const { return phaseAngleQueryHz_; }
    int angleErrorQueryHz() const { return angleErrorQueryHz_; }
    bool busy() const { return operation_ != DeviceOperation::None; }
    DeviceOperation operation() const { return operation_; }
    DeviceAddress address() const { return address_; }
    DeviceSession session() const { return session_; }
    Motion target() const { return target_; }

    // 命令排队发送给 ioThread_ 中的 DeviceService。
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
    void calibrate(bool output);
    void cancelCalibration();
    void startInspection(const std::array<float, 4>&);
    void stopInspection();
    void readParameters();
    void writeParameters(const QList<double>&, ParameterGroup, bool persist);
    void startFirmwareUpdate(const QString& filePath);

  signals:
    void stateChanged();
    void responseFrame(studio::CanFrame frame);
    void telemetryReceived(studio::Telemetry);
    void refreshRateChanged(int repliesPerSecond);
    void motionSampleReceived(studio::Telemetry feedback, double sampleSeconds,
                              studio::Motion target);
    void targetChanged(studio::Motion);
    void motionStateChanged(bool active);
    void encoderPoint(double time, double value);
    void linearityStateChanged(bool active);
    void phaseAngleReceived(studio::PhaseAngleData data);
    void phaseAngleQueryStateChanged(bool active);
    void angleErrorReceived(studio::AngleErrorData data);
    void angleErrorQueryStateChanged(bool active);
    void canFdCountersReceived(studio::CanFdCounters counters);
    void canFdDataRateRequestSent(quint8 requested, bool success);
    void canFdDataRateResponse(quint8 requested, quint8 actual, bool success);
    void inputCalibrationCommandResponse(quint16 result);
    void inputCalibrationReportReceived(quint16 step, quint16 data0, quint16 data1, quint16 data2);
    void inputCalibrationPoint(int series, double value);
    void outputCalibrationCommandResponse(quint16 result);
    void outputCalibrationResultReceived(quint16 data0, quint16 data1, quint16 data2);
    void inspectionStatusChanged(QString status);
    void inspectionStatusWordChanged(quint16 word, QString text);
    void statusWordReceived(quint16 word);
    void inspectionStep(int step);
    void inspectionDataReceived(studio::TestResult result);
    void inspectionFinished(studio::TestResult result);
    void parametersReceived(QList<double> values);
    void firmwareProgress(int percent);
    void firmwareStatusChanged(QString status);
    void versionChanged(QString version);
    void message(QString text);

  private:
    // 此门面只跨越线程边界并转发强类型状态与信号，不解析协议。
    void post(std::function<void(DeviceService*)> operation);
    void publishState(DeviceService* source);
    void wireWorker();

    QThread ioThread_;
    QObject* host_ = nullptr;
    DeviceService* worker_ = nullptr;
    ConnectionState connectionState_ = ConnectionState::Disconnected;
    bool enabled_ = false;
    bool motionActive_ = false;
    bool linearityActive_ = false;
    bool phaseAngleQueryActive_ = false;
    bool angleErrorQueryActive_ = false;
    int phaseAngleQueryHz_ = 100;
    int angleErrorQueryHz_ = 100;
    DeviceOperation operation_ = DeviceOperation::None;
    DeviceAddress address_;
    DeviceSession session_;
    Motion target_;
};

} // 命名空间 studio
