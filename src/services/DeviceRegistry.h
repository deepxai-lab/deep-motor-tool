#pragma once
#include "domain/Types.h"
#include <QObject>
#include <QSettings>
#include <memory>

namespace studio {
// 这里只保存配置：允许任意板卡和电机，不设特定型号设备白名单。
class DeviceRegistry final : public QObject {
    Q_OBJECT
  public:
    explicit DeviceRegistry(QString settingsFile = {}, QObject* parent = nullptr);
    const QList<BoardProfile>& boards() const { return boards_; }
    const QList<DeviceAddress>& motors() const { return motors_; }
    bool addBoard(BoardProfile);
    bool removeBoard(const QString& ip);
    bool addMotor(DeviceAddress);
    bool removeMotor(int index);
    bool storeDiscoveredSession(const DeviceSession&);
  signals:
    void changed();
    void errorOccurred(QString message);

  private:
    void load();
    void save();
    QList<BoardProfile> boards_;
    QList<DeviceAddress> motors_;
    std::unique_ptr<QSettings> settings_;
};
} // 命名空间 studio
