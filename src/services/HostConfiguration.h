#pragma once
#include <QString>

namespace studio {

struct PeriodicQueryRates {
    int phaseAngleHz = 100;
    int angleErrorHz = 100;
};

class HostConfiguration final {
  public:
    static QString filePath();
    static PeriodicQueryRates periodicQueryRates();
    static PeriodicQueryRates periodicQueryRates(const QString& path);
};

} // 命名空间 studio
