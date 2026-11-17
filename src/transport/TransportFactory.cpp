#include "TransportFactory.h"
#include "SimulatedTransport.h"
#include "UdpTransport.h"
#include "protocol/ProtocolRegistry.h"

namespace studio {
std::unique_ptr<Transport> TransportFactory::create(TransportKind kind, const QString& protocolId) {
    if (kind == TransportKind::Simulation)
        return std::make_unique<SimulatedTransport>();
    auto protocol = ProtocolRegistry::create(protocolId);
    return protocol ? std::make_unique<UdpTransport>(std::move(protocol)) : nullptr;
}
} // 命名空间 studio
