#pragma once

#include "domain/Types.h"

namespace studio {

class CanFdFrameCodec final {
  public:
    static constexpr int TxContainerBytes = 74;
    static constexpr int RxContainerBytes = 82;

    static QByteArray encodeTx(const CanFrame&);
    static QByteArray encodeRx(const CanFrame&); // 供模拟器与协议测试使用
    static bool decodeTx(const QByteArray& container, CanFrame& frame, QString& error);
    static bool decodeRx(const QByteArray& container, CanFrame& frame, QString& error);

  private:
    static QByteArray encode(const CanFrame&, int containerBytes, bool response);
    static bool decode(const QByteArray&, int containerBytes, bool response, CanFrame&, QString&);
};

} // 命名空间 studio
