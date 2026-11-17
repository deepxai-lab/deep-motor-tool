#pragma once
#include "Protocol.h"
#include <QStringList>
#include <functional>
#include <memory>

namespace studio {
// 集中的协议扩展点。新线上协议通过稳定 ID 注册创建器；
// 传输层和服务层无需包含具体编解码器头文件。
class ProtocolRegistry final {
  public:
    using Creator = std::function<std::unique_ptr<Protocol>()>;
    static bool registerProtocol(const QString& id, Creator creator);
    static std::unique_ptr<Protocol> create(const QString& id);
    static QStringList ids();
};
} // 命名空间 studio
