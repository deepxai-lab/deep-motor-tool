#pragma once
#include "Transport.h"
#include <QElapsedTimer>
#include <QTimer>

namespace studio {
// 确定性的协议级模拟器，绝不打开网络套接字。
class SimulatedTransport final : public Transport {
    Q_OBJECT
  public:
    explicit SimulatedTransport(QObject* parent = nullptr);
    bool open(const DeviceSession&) override;
    void close() override;
    bool send(const CanFrame&) override;
    bool isOpen() const override { return timer_.isActive(); }

  private:
    void reply(quint8 command, QByteArray payload);
    void tick();
    DeviceAddress address_;
    QTimer timer_;
    QElapsedTimer clock_;
    bool enabled_ = false;
    Motion target_;
    float position_ = 0;
    int calibration_ = -1, outputCalibration_ = -1, test_ = -1;
    QByteArray config_;
};
} // 命名空间 studio
