#pragma once
#include "Pages.h"
#include "services/DeviceRegistry.h"
#include "transport/TransportFactory.h"
#include <QComboBox>
#include <QLabel>
#include <QMainWindow>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QTimer>

class QDialog;
class QFrame;
class QColor;

namespace studio::ui {
class MainWindow final : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(QString dataDirectory = {}, TransportKind transport = TransportKind::Udp,
                        QWidget* parent = nullptr);
    ~MainWindow() override;
    void selectPage(int index);
    void startDemoMotion();
    void startLoopbackDemo();
    void capturePages(const QString& directory);
    int pageCount() const { return pages_ ? pages_->count() : 0; }
    int currentPage() const { return pages_ ? pages_->currentIndex() : -1; }
    bool captureDeviceDialog(const QString& path);
    bool captureAuxiliaryDialogs(const QString& directory);
    bool nativeSystemMenuInstalled() const { return nativeMenuInstalled_; }

  protected:
    bool nativeEvent(const QByteArray& eventType, void* message, qintptr* result) override;

  private:
    void createAppearanceActions();
    void installNativeSystemMenu();
    void updateNativeThemeChecks();
    void applyBackgroundTheme(const QColor& color);
    void showReleaseNotes();
    void manageDevices();
    void openAutoConnect();
    void firmwareDialog();
    void storageDialog();
    bool setRecordingDirectory(const QString& directory);
    void updateState();
    void resetStatusWordDisplay(bool connected, bool cleared = false);
    void showStatusWord(quint16 word);
    void log(QString message);
    void loadProfiles();
    AsyncDeviceService device_;
    RecordingService motionRecorder_, temperatureRecorder_;
    Identity identity_;
    DeviceRegistry registry_;
    QComboBox* deviceCombo_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QLabel *title_ = nullptr, *connection_ = nullptr, *connectionIdentity_ = nullptr,
           *refreshRate_ = nullptr, *version_ = nullptr, *operation_ = nullptr,
           *statusWordValue_ = nullptr,
           *statusWordTime_ = nullptr;
    QFrame* statusWordPanel_ = nullptr;
    QList<QLabel*> metrics_;
    QList<QPushButton*> navigation_;
    QTimer metricRefreshTimer_;
    QTimer logFlushTimer_;
    QStringList pendingLogLines_;
    QString pendingStatusMessage_;
    Telemetry latestTelemetry_;
    bool metricRefreshPending_ = false;
    bool hasStatusWordResponse_ = false;
    bool nativeMenuInstalled_ = false;
    quintptr nativeThemeMenu_ = 0;
    QPlainTextEdit* logs_ = nullptr;
    QPushButton *connectButton_ = nullptr, *autoConnect_ = nullptr, *parameters_ = nullptr,
                *firmware_ = nullptr, *storage_ = nullptr, *statusWordClear_ = nullptr;
};
} // 命名空间 studio::ui
