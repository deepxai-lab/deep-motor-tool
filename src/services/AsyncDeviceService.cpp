#include "AsyncDeviceService.h"
#include <QCoreApplication>
#include <QMetaObject>

namespace studio {

AsyncDeviceService::AsyncDeviceService(TransportKind kind, QObject* parent)
    : QObject(parent), host_(new QObject) {
    ioThread_.setObjectName(QStringLiteral("device-communication"));
    host_->moveToThread(&ioThread_);
    ioThread_.start(QThread::HighPriority);

    // 在最终所属线程中构造服务。DeviceService 拥有值成员 QTimer，
    // 传输对象拥有 QUdpSocket；若先构造再移动，这些对象会保留错误的线程归属。
    QMetaObject::invokeMethod(host_, [this, kind] {
        worker_ = new DeviceService(TransportFactory::create(kind), host_);
        wireWorker();
    }, Qt::BlockingQueuedConnection);
    phaseAngleQueryHz_ = worker_->phaseAngleQueryHz();
    angleErrorQueryHz_ = worker_->angleErrorQueryHz();
}

AsyncDeviceService::~AsyncDeviceService() {
    if (host_ && ioThread_.isRunning()) {
        QMetaObject::invokeMethod(host_, [this] {
            if (worker_) {
                worker_->disconnectDevice();
                delete worker_;
                worker_ = nullptr;
            }
            host_->moveToThread(QCoreApplication::instance()->thread());
        }, Qt::BlockingQueuedConnection);
    }
    ioThread_.quit();
    ioThread_.wait();
    delete host_;
    host_ = nullptr;
}

void AsyncDeviceService::post(std::function<void(DeviceService*)> operation) {
    if (!host_ || !ioThread_.isRunning())
        return;
    QMetaObject::invokeMethod(host_, [this, operation = std::move(operation)] {
        if (worker_)
            operation(worker_);
    }, Qt::QueuedConnection);
}

void AsyncDeviceService::publishState(DeviceService* source) {
    const auto connectionState = source->connectionState();
    const bool enabled = source->enabled();
    const auto operation = source->operation();
    const auto address = source->address();
    const auto session = source->session();
    const auto target = source->target();
    QMetaObject::invokeMethod(this, [this, connectionState, enabled, operation,
                                     address, session, target] {
        connectionState_ = connectionState;
        enabled_ = enabled;
        operation_ = operation;
        address_ = address;
        session_ = session;
        target_ = target;
        emit stateChanged();
    }, Qt::QueuedConnection);
}

void AsyncDeviceService::wireWorker() {
    connect(worker_, &DeviceService::stateChanged, worker_, [this] { publishState(worker_); });
    connect(worker_, &DeviceService::motionStateChanged, this, [this](bool active) {
        motionActive_ = active;
        emit motionStateChanged(active);
    }, Qt::QueuedConnection);
    connect(worker_, &DeviceService::linearityStateChanged, this, [this](bool active) {
        linearityActive_ = active;
        emit linearityStateChanged(active);
    }, Qt::QueuedConnection);
    connect(worker_, &DeviceService::phaseAngleQueryStateChanged, this, [this](bool active) {
        phaseAngleQueryActive_ = active;
        emit phaseAngleQueryStateChanged(active);
    }, Qt::QueuedConnection);
    connect(worker_, &DeviceService::angleErrorQueryStateChanged, this, [this](bool active) {
        angleErrorQueryActive_ = active;
        emit angleErrorQueryStateChanged(active);
    }, Qt::QueuedConnection);
    connect(worker_, &DeviceService::targetChanged, this, [this](Motion target) {
        target_ = target;
        emit targetChanged(target);
    }, Qt::QueuedConnection);
    connect(worker_, &DeviceService::motionSampleReceived, this,
            [this](Telemetry feedback, double sampleSeconds, Motion target) {
        target_ = target;
        emit motionSampleReceived(feedback, sampleSeconds, target);
    }, Qt::QueuedConnection);

#define FORWARD(signalName) \
    connect(worker_, &DeviceService::signalName, this, &AsyncDeviceService::signalName, Qt::QueuedConnection)
    FORWARD(responseFrame);
    FORWARD(telemetryReceived);
    FORWARD(refreshRateChanged);
    FORWARD(encoderPoint);
    FORWARD(phaseAngleReceived);
    FORWARD(angleErrorReceived);
    FORWARD(canFdCountersReceived);
    FORWARD(canFdDataRateRequestSent);
    FORWARD(canFdDataRateResponse);
    FORWARD(inputCalibrationCommandResponse);
    FORWARD(inputCalibrationReportReceived);
    FORWARD(inputCalibrationPoint);
    FORWARD(outputCalibrationCommandResponse);
    FORWARD(outputCalibrationResultReceived);
    FORWARD(inspectionStatusChanged);
    FORWARD(inspectionStatusWordChanged);
    FORWARD(statusWordReceived);
    FORWARD(inspectionStep);
    FORWARD(inspectionDataReceived);
    FORWARD(inspectionFinished);
    FORWARD(parametersReceived);
    FORWARD(firmwareProgress);
    FORWARD(firmwareStatusChanged);
    FORWARD(versionChanged);
    FORWARD(message);
#undef FORWARD

    publishState(worker_);
}

void AsyncDeviceService::connectDevice(DeviceAddress address) {
    connectSession(DeviceSession{std::move(address)});
}
void AsyncDeviceService::connectSession(DeviceSession session) {
    session_ = session;
    address_ = session.address;
    post([session = std::move(session)](DeviceService* service) { service->connectSession(session); });
}
void AsyncDeviceService::disconnectDevice() {
    post([](DeviceService* service) { service->disconnectDevice(); });
}
void AsyncDeviceService::setEnabled(bool enabled) {
    post([enabled](DeviceService* service) { service->setEnabled(enabled); });
}
void AsyncDeviceService::command(Command command) {
    post([command](DeviceService* service) { service->command(command); });
}
void AsyncDeviceService::startMotion(const MotionProgram& program) {
    post([program](DeviceService* service) { service->startMotion(program); });
}
void AsyncDeviceService::stopMotion() {
    post([](DeviceService* service) { service->stopMotion(); });
}
void AsyncDeviceService::resetMotionTimeline() {
    post([](DeviceService* service) { service->resetMotionTimeline(); });
}
void AsyncDeviceService::startLinearity() {
    post([](DeviceService* service) { service->startLinearity(); });
}
void AsyncDeviceService::stopLinearity() {
    post([](DeviceService* service) { service->stopLinearity(); });
}
void AsyncDeviceService::startPhaseAngleQuery() {
    post([](DeviceService* service) { service->startPhaseAngleQuery(); });
}
void AsyncDeviceService::stopPhaseAngleQuery() {
    post([](DeviceService* service) { service->stopPhaseAngleQuery(); });
}
void AsyncDeviceService::startAngleErrorQuery() {
    post([](DeviceService* service) { service->startAngleErrorQuery(); });
}
void AsyncDeviceService::stopAngleErrorQuery() {
    post([](DeviceService* service) { service->stopAngleErrorQuery(); });
}
void AsyncDeviceService::queryCanFdCounters() {
    post([](DeviceService* service) { service->queryCanFdCounters(); });
}
void AsyncDeviceService::setCanFdDataRate(quint8 rate) {
    post([rate](DeviceService* service) { service->setCanFdDataRate(rate); });
}
void AsyncDeviceService::calibrate(bool output) {
    post([output](DeviceService* service) { service->calibrate(output); });
}
void AsyncDeviceService::cancelCalibration() {
    post([](DeviceService* service) { service->cancelCalibration(); });
}
void AsyncDeviceService::startInspection(const std::array<float, 4>& scope) {
    post([scope](DeviceService* service) { service->startInspection(scope); });
}
void AsyncDeviceService::stopInspection() {
    post([](DeviceService* service) { service->stopInspection(); });
}
void AsyncDeviceService::readParameters() {
    post([](DeviceService* service) { service->readParameters(); });
}
void AsyncDeviceService::writeParameters(const QList<double>& values, ParameterGroup group, bool persist) {
    post([values, group, persist](DeviceService* service) {
        service->writeParameters(values, group, persist);
    });
}
void AsyncDeviceService::startFirmwareUpdate(const QString& filePath) {
    post([filePath](DeviceService* service) { service->startFirmwareUpdate(filePath); });
}

} // 命名空间 studio
