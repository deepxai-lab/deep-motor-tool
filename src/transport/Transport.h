#pragma once
#include "domain/Types.h"
#include <QObject>

namespace studio {
class Transport : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    virtual bool open(const DeviceSession&) = 0;
    virtual void close() = 0;
    virtual bool send(const CanFrame&) = 0;
    virtual bool isOpen() const = 0;
    // 部分物理适配器需要先应用链路配置，之后才能接收第一条 CAN 请求。
    // 内存传输和已经就绪的传输继续使用默认同步行为。
    virtual int initialRequestDelayMs() const { return 0; }
  signals:
    void frameReceived(studio::CanFrame frame);
    void errorOccurred(QString message);
};
} // 命名空间 studio
