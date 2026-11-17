#pragma once
#include "Transport.h"
#include <memory>

namespace studio {
enum class TransportKind { Simulation, Udp };

// 构造过程放在 DeviceService 之外，使程序可在不改业务逻辑的情况下选择
// 模拟器、UDP 或未来的 TCP、串口适配器。
class TransportFactory final {
  public:
    static std::unique_ptr<Transport> create(
        TransportKind kind, const QString& protocolId = "deep.ethercan.canfd.v1");
};
} // 命名空间 studio
