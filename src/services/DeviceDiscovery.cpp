#include "DeviceDiscovery.h"
#include "transport/TransportFactory.h"
#include <QHostAddress>

namespace studio {

namespace {
constexpr int CandidateTimeoutMs = 900;
constexpr auto ProtocolId = "deep.ethercan.canfd.v1";
}

DeviceDiscovery::DeviceDiscovery(QObject* parent)
    : DeviceDiscovery(TransportFactory::create(TransportKind::Udp), parent) {}

DeviceDiscovery::DeviceDiscovery(std::unique_ptr<Transport> transport, QObject* parent)
    // probe_ 是值成员，其生命周期已经与当前对象绑定。
    // 不要再将其注册为 QObject 子对象；双重所有权模型没有必要，
    // 也会增加重复构造、析构时的审计难度。
    : QObject(parent), probe_(std::move(transport), nullptr) {
    attemptTimer_.setSingleShot(true);
    attemptTimer_.setInterval(CandidateTimeoutMs);
    connect(&attemptTimer_, &QTimer::timeout, this, [this] {
        if (!active_)
            return;
        probe_.disconnectDevice();
        startNextCandidate();
    });
    connect(&probe_, &DeviceService::versionChanged, this, [this](const QString& version) {
        if (version != QString::fromUtf8("—"))
            version_ = version;
    });
    connect(&probe_, &DeviceService::stateChanged, this, [this] {
        if (!active_)
            return;
        if (probe_.connected()) {
            attemptTimer_.stop();
            const auto session = candidates_.at(candidateIndex_);
            const auto version = version_;
            active_ = false;
            probe_.disconnectDevice();
            emit deviceFound(session, version);
            emit scanFinished(attempted_);
        } else if (probe_.connectionFailed()) {
            attemptTimer_.stop();
            QTimer::singleShot(0, this, &DeviceDiscovery::startNextCandidate);
        }
    });
}

QList<DeviceSession> DeviceDiscovery::candidates(const QString& ip, quint8 channel) {
    QList<DeviceSession> result;
    result.reserve(32);
    for (const quint32 bitrate : {quint32(5000000), quint32(8000000)})
        for (int motor = 0; motor <= 15; ++motor)
            result.append({{ip, channel, quint8(motor)}, bitrate, ProtocolId});
    return result;
}

bool DeviceDiscovery::start(QString ip, int channel) {
    ip = ip.trimmed();
    const QHostAddress address(ip);
    if (address.protocol() != QAbstractSocket::IPv4Protocol || channel < 0 || channel > 3) {
        emit validationFailed(QStringLiteral("请输入有效 IPv4 地址和 CAN 通道"));
        return false;
    }
    if (active_)
        stop();
    candidates_ = candidates(address.toString(), quint8(channel));
    candidateIndex_ = -1;
    attempted_ = 0;
    version_.clear();
    active_ = true;
    emit scanStarted(candidates_.size());
    startNextCandidate();
    return true;
}

void DeviceDiscovery::stop() {
    if (!active_)
        return;
    active_ = false;
    attemptTimer_.stop();
    probe_.disconnectDevice();
    emit scanStopped(attempted_);
}

void DeviceDiscovery::startNextCandidate() {
    if (!active_)
        return;
    ++candidateIndex_;
    if (candidateIndex_ >= candidates_.size()) {
        active_ = false;
        probe_.disconnectDevice();
        emit scanFinished(attempted_);
        return;
    }
    version_.clear();
    attempted_ = candidateIndex_ + 1;
    const auto session = candidates_.at(candidateIndex_);
    emit attemptStarted(attempted_, candidates_.size(), session);
    probe_.connectSession(session);
    if (active_ && !probe_.connected())
        attemptTimer_.start();
}

} // 命名空间 studio

