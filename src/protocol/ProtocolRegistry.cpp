#include "ProtocolRegistry.h"
#include "LegacyCanFdCodec.h"
#include <QHash>

namespace studio {
namespace {
QHash<QString, ProtocolRegistry::Creator>& creators() {
    static QHash<QString, ProtocolRegistry::Creator> values{
        {"deep.ethercan.canfd.v1", [] { return std::make_unique<LegacyCanFdCodec>(); }}};
    return values;
}
}

bool ProtocolRegistry::registerProtocol(const QString& id, Creator creator) {
    const auto key = id.trimmed();
    if (key.isEmpty() || !creator || creators().contains(key))
        return false;
    creators().insert(key, std::move(creator));
    return true;
}

std::unique_ptr<Protocol> ProtocolRegistry::create(const QString& id) {
    const auto found = creators().constFind(id.trimmed());
    return found == creators().cend() ? nullptr : found.value()();
}

QStringList ProtocolRegistry::ids() {
    auto result = creators().keys();
    result.sort();
    return result;
}
} // 命名空间 studio
