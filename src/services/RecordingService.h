#pragma once
#include "domain/Types.h"
#include <QFile>
#include <QObject>
#include <QTimer>

namespace studio {
class RecordingService final : public QObject {
    Q_OBJECT
  public:
    explicit RecordingService(QString directory = {}, QObject* parent = nullptr);
    bool start(bool temperatureOnly);
    void stop();
    bool isActive() const { return file_.isOpen(); }
    bool hasAutomaticCloseDeadline() const { return autoCloseTimer_.isActive(); }
    QString directory() const { return directory_; }
    bool setDirectory(QString directory);
    void append(const Telemetry&, const Motion&);
    void appendMotionSample(double seconds, const Telemetry&, const Motion&);
    bool saveTest(const Identity&, const DeviceAddress&, const TestResult&);
    static QByteArray csvCell(QString value);
  signals:
    void changed(bool active, QString path);
    void directoryChanged(QString path);
    void errorOccurred(QString message);

  private:
    bool write(const QByteArray&);
    QString directory_;
    QFile file_;
    QTimer flushTimer_, autoCloseTimer_;
    bool temperatureOnly_ = false;
};
} // 命名空间 studio
