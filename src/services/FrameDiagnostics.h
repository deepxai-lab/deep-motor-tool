#pragma once
#include "domain/Types.h"
#include <functional>
#include <QStringList>

namespace studio {
// 诊断页使用的只读帧解释器。字节解析保留在界面边界以下，
// 展示语言通过文本解析器注入。
class FrameDiagnostics final {
  public:
    using TextResolver = std::function<QString(const QString&)>;
    static QString commandName(quint8 command, const TextResolver& resolve = {});
    static QString summary(const CanFrame& frame, const TextResolver& resolve = {});
    // 每个置位的 STATUSWORD 位对应一项。includeCodes 为真时，
    // 每项包含协议位掩码，例如“过流保护 (0x0004)”。
    static QStringList statusWordFaults(quint16 statusWord, bool includeCodes = false,
                                        const TextResolver& resolve = {});
};
} // 命名空间 studio
