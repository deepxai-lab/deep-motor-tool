#pragma once
#include "domain/Types.h"
#include <QList>

namespace studio {
struct DecodeResult {
    QList<CanFrame> frames;
    QString error;
    bool ok() const { return error.isEmpty(); }
};
// 协议适配器独占线上数据布局，不包含界面、套接字、本机结构体填充或编译器位域。
class Protocol {
  public:
    virtual ~Protocol() = default;
    virtual QString id() const = 0;
    virtual QByteArray encode(const CanFrame&, quint8 sequence) const = 0;
    virtual DecodeResult decode(const QByteArray&) const = 0;
    virtual QByteArray configuration(quint8 board, quint32 dataBitrate, quint8 sequence) const = 0;
};
} // 命名空间 studio
