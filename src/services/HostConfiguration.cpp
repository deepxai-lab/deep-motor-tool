#include "HostConfiguration.h"
#include <QCoreApplication>
#include <QFile>
#include <QSettings>

namespace studio {
namespace {
constexpr int SafeFallbackHz = 100;
constexpr int MaximumDiagnosticHz = 500;

int queryRate(QSettings& settings, const QString& key) {
    bool ok = false;
    const int value = settings.value(key).toInt(&ok);
    return ok && value >= 1 && value <= MaximumDiagnosticHz ? value : SafeFallbackHz;
}
} // 匿名命名空间

QString HostConfiguration::filePath() {
    const QString directory = QCoreApplication::applicationDirPath();
    const QString active = directory + "/config.ini";
    if (!QFile::exists(active)) {
        const QString defaults = directory + "/config.default.ini";
        if (QFile::exists(defaults))
            QFile::copy(defaults, active);
    }
    return active;
}

PeriodicQueryRates HostConfiguration::periodicQueryRates() {
    return periodicQueryRates(filePath());
}

PeriodicQueryRates HostConfiguration::periodicQueryRates(const QString& path) {
    QSettings settings(path, QSettings::IniFormat);
    return {queryRate(settings, "periodic_queries/uvw_phase_angle_hz"),
            queryRate(settings, "periodic_queries/angle_error_hz")};
}

} // 命名空间 studio
