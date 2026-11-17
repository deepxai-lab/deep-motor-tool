#pragma once

#include "DeviceService.h"
#include <QList>
#include <QTimer>
#include <memory>

namespace studio {

// 使用与普通连接相同的版本握手探测真机。
// 它拥有隔离的 UDP 会话；发现设备后，MainWindow 仍通过常规 DeviceService
// 重新连接，因此后续行为保持不变。
class DeviceDiscovery final : public QObject {
    Q_OBJECT
  public:
    explicit DeviceDiscovery(QObject* parent = nullptr);
    DeviceDiscovery(std::unique_ptr<Transport> transport, QObject* parent);

    bool active() const { return active_; }
    int attempted() const { return attempted_; }
    static QList<DeviceSession> candidates(const QString& ip, quint8 channel);

  public slots:
    bool start(QString ip, int channel);
    void stop();

  signals:
    void scanStarted(int total);
    void attemptStarted(int current, int total, studio::DeviceSession session);
    void deviceFound(studio::DeviceSession session, QString version);
    void scanFinished(int attempted);
    void scanStopped(int attempted);
    void validationFailed(QString reason);

  private:
    void startNextCandidate();
    DeviceService probe_;
    QTimer attemptTimer_;
    QList<DeviceSession> candidates_;
    int candidateIndex_ = -1;
    int attempted_ = 0;
    bool active_ = false;
    QString version_;
};

} // 命名空间 studio

