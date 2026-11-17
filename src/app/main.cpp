#include "ui/MainWindow.h"
#include "ui/Localization.h"
#include "ui/Theme.h"
#include "qcustomplot.h"
#include <QAction>
#include <QApplication>
#include <QAbstractItemView>
#include <QComboBox>
#include <QDir>
#include <QDialog>
#include <QEvent>
#include <QFontDatabase>
#include <QIcon>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QSet>
#include <QTemporaryDir>
#include <QTimer>
#include <memory>

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    app.setOrganizationName("DEEP Robotics");
    app.setApplicationName("JointMotorTool");
    app.setApplicationVersion(APP_VERSION);
    app.setWindowIcon(QIcon(":/resources/branding/deep-app-icon.svg"));
    {
        QSettings current;
        const QList<QPair<QString, QString>> legacyProfiles{{"MotorStudio", "MotorStudio"},
                                                            {"MotorStudio", "MotorStudioPreview"}};
        for (const auto& profile : legacyProfiles) {
            QSettings legacy(profile.first, profile.second);
            for (const auto& key : legacy.allKeys())
                if (!current.contains(key))
                    current.setValue(key, legacy.value(key));
        }
    }
    app.setStyle("Fusion");
    auto args = app.arguments();
    const bool smoke = args.contains("--smoke-test");
    const bool lifecycle = args.contains("--lifecycle-test");
    const int capture = args.indexOf("--capture");
    const int deviceCapture = args.indexOf("--capture-device-dialog");
    const int auxiliaryCapture = args.indexOf("--capture-auxiliary-dialogs");
    const bool loopbackDemo = args.contains("--loopback-demo");
    if (app.platformName() == "offscreen") {
        // Windows 离屏插件不会自动枚举系统字体，因此手动载入中文和英文字体。
        QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR", "C:/Windows") + "/Fonts/msyh.ttc");
        QFontDatabase::addApplicationFont(qEnvironmentVariable("WINDIR", "C:/Windows") +
                                          "/Fonts/segoeui.ttf");
        app.setFont(QFont("Microsoft YaHei", 9));
    }
    std::unique_ptr<QTemporaryDir> temp;
    QString data;
    if (smoke || lifecycle || capture >= 0 || deviceCapture >= 0 || auxiliaryCapture >= 0) {
        temp = std::make_unique<QTemporaryDir>();
        if (!temp->isValid())
            return 2;
        data = temp->path() + "/records";
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, temp->path());
    }
    const int languageArgument = args.indexOf("--language");
    if (languageArgument >= 0 && languageArgument + 1 < args.size())
        studio::ui::i18n::setLanguageCode(args[languageArgument + 1]);
    const int backgroundArgument = args.indexOf("--background");
    if (temp && backgroundArgument >= 0 && backgroundArgument + 1 < args.size())
        QSettings().setValue("ui/backgroundColor", args[backgroundArgument + 1]);
    const QColor background = studio::ui::savedBackground();
    app.setPalette(studio::ui::themePalette(background));
    app.setStyleSheet(studio::ui::styleSheet(background));
    if (lifecycle) {
        QSet<QWidget*> baseline;
        for (auto* widget : QApplication::topLevelWidgets())
            baseline.insert(widget);
        for (int iteration = 0; iteration < 40; ++iteration) {
            auto* candidate = new studio::ui::MainWindow(data, studio::TransportKind::Simulation);
            QPointer<studio::ui::MainWindow> guard(candidate);
            candidate->show();
            for (int page = 0; page < candidate->pageCount(); ++page)
                candidate->selectPage(page);
            // 生命周期检查同时打开子窗口；这里不启动搜索或设备命令。
            for (const char* name : {"autoConnectEntry", "deviceManageButton", "storageSettingsButton",
                                     "motionParametersButton", "curveFollowButton"}) {
                if (auto* button = candidate->findChild<QPushButton*>(name)) {
                    button->setEnabled(true);
                    button->click();
                }
            }
            if (auto* releaseNotes = candidate->findChild<QAction*>("releaseNotesAction"))
                releaseNotes->trigger();
            app.processEvents();
            delete candidate;
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            app.processEvents();
            if (guard)
                return 9;
            for (auto* widget : QApplication::topLevelWidgets())
                if (!baseline.contains(widget))
                    return 10;
        }
        return 0;
    }
    const bool preview = !loopbackDemo &&
                         (smoke || capture >= 0 || deviceCapture >= 0 || auxiliaryCapture >= 0 ||
                          args.contains("--simulation"));
    studio::ui::MainWindow window(data, preview ? studio::TransportKind::Simulation
                                                : studio::TransportKind::Udp);
    window.show();
    if (loopbackDemo)
        window.startLoopbackDemo();
    if (smoke) {
        QApplication::processEvents();
        if (app.windowIcon().isNull() || window.windowIcon().isNull())
            return 7;
        if (window.currentPage() != 1)
            return 6;
        for (auto* combo : window.findChildren<QComboBox*>())
            if (combo->view()->palette().color(QPalette::Base).lightness() > 80)
                return 5;
        auto* deviceId = window.findChild<QLineEdit*>("barcodeDeviceId");
        auto* serial = window.findChild<QLineEdit*>("barcodeSerial");
        auto* confirm = window.findChild<QPushButton*>("barcodeConfirmButton");
        auto* parameters = window.findChild<QPushButton*>("parametersButton");
        auto* autoConnect = window.findChild<QPushButton*>("autoConnectEntry");
        auto* phaseQuery = window.findChild<QPushButton*>("startPhaseAngleQuery");
        auto* angleErrorQuery = window.findChild<QPushButton*>("startAngleErrorQuery");
        auto* counterQuery = window.findChild<QPushButton*>("queryCanFdCounters");
        auto* statusWord = window.findChild<QLabel*>("globalStatusWordValue");
        auto* statusWordClear = window.findChild<QPushButton*>("statusWordClear");
        auto* refreshRate = window.findChild<QLabel*>("refreshRate");
        auto* releaseNotes = window.findChild<QAction*>("releaseNotesAction");
        auto* graphite = window.findChild<QAction*>("backgroundGraphiteAction");
        if (!deviceId || !serial || !confirm || !parameters || !autoConnect || !phaseQuery ||
            !angleErrorQuery || !counterQuery || !statusWord || !statusWordClear || !refreshRate ||
            !releaseNotes || !graphite)
            return 3;
        const QString displayedVersion = "V" + app.applicationVersion();
        if (!window.windowTitle().contains(displayedVersion))
            return 12;
        if (app.platformName() == "windows" && !window.nativeSystemMenuInstalled())
            return 17;
        releaseNotes->trigger();
        QApplication::processEvents();
        auto* releaseDialog = window.findChild<QDialog*>("releaseNotesDialog");
        auto* releaseText = releaseDialog
                                ? releaseDialog->findChild<QLabel*>("releaseVersion")
                                : nullptr;
        if (!releaseDialog || !releaseDialog->isVisible() || !releaseText ||
            !releaseText->text().contains(displayedVersion))
            return 13;
        releaseDialog->close();
        graphite->trigger();
        QApplication::processEvents();
        if (studio::ui::savedBackground() != QColor("#22252b") ||
            app.palette().color(QPalette::Window) != QColor("#22252b"))
            return 15;
        const auto plots = window.findChildren<QCustomPlot*>();
        if (plots.isEmpty() ||
            plots.first()->axisRect()->backgroundBrush().color() !=
                studio::ui::ThemeColors(QColor("#22252b")).plot)
            return 16;
        auto* light = window.findChild<QAction*>("backgroundLightAction");
        if (!light)
            return 18;
        light->trigger();
        QApplication::processEvents();
        const studio::ui::ThemeColors lightColors(QColor("#f1f4f8"));
        if (studio::ui::savedBackground() != QColor("#f1f4f8") ||
            app.palette().color(QPalette::Window) != QColor("#f1f4f8") ||
            plots.first()->axisRect()->backgroundBrush().color() != lightColors.plot ||
            studio::ui::contrastRatio(plots.first()->xAxis->tickLabelColor(), lightColors.plot) < 4.5)
            return 19;
        // 未连接时不能开放参数写入入口。
        if (parameters->isEnabled())
            return 8;
        deviceId->setText("SMOKE-ID");
        serial->setText("SMOKE-SERIAL");
        confirm->click();
        if (window.currentPage() != 1)
            return 4;
        window.selectPage(0);
    }
    const int captureSize = args.indexOf("--capture-size");
    if (captureSize >= 0 && captureSize + 1 < args.size()) {
        const auto parts = args[captureSize + 1].split('x');
        if (parts.size() == 2)
            window.resize(parts[0].toInt(), parts[1].toInt());
    }
    int captureDelayMs = capture >= 0 ? 4500 : 800;
    const int captureDelay = args.indexOf("--capture-delay-ms");
    if (captureDelay >= 0 && captureDelay + 1 < args.size())
        captureDelayMs = qBound(100, args[captureDelay + 1].toInt(), 120000);
    if (smoke || capture >= 0 || deviceCapture >= 0 || auxiliaryCapture >= 0) {
        if (!loopbackDemo && (smoke || capture >= 0 || auxiliaryCapture >= 0))
            window.startDemoMotion();
        QTimer::singleShot(captureDelayMs, &app, [&] {
            // 冒烟测试会启动模拟运动；未点击记录按钮时不得生成运动 CSV。
            if (smoke && !QDir(data).entryList({"motion*.csv"}, QDir::Files).isEmpty()) {
                app.exit(11);
                return;
            }
            if (capture >= 0 && capture + 1 < args.size()) {
                window.capturePages(QDir(args[capture + 1]).absolutePath());
                if (auto* releaseNotes = window.findChild<QAction*>("releaseNotesAction")) {
                    releaseNotes->trigger();
                    app.processEvents();
                    if (auto* dialog = window.findChild<QDialog*>("releaseNotesDialog")) {
                        dialog->grab().save(
                            QDir(args[capture + 1]).filePath("release-notes.png"));
                        dialog->close();
                    }
                }
            }
            if (deviceCapture >= 0 && deviceCapture + 1 < args.size())
                window.captureDeviceDialog(QDir(args[deviceCapture + 1]).absolutePath());
            if (auxiliaryCapture >= 0 && auxiliaryCapture + 1 < args.size())
                window.captureAuxiliaryDialogs(QDir(args[auxiliaryCapture + 1]).absolutePath());
            for (int i = 0; i < window.pageCount(); ++i) {
                window.selectPage(i);
                app.processEvents();
            }
            app.quit();
        });
    }
    return app.exec();
}
