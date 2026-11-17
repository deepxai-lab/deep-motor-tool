#include "MainWindow.h"
#include "AutoConnectDialog.h"
#include "DeviceManagerDialog.h"
#include "Localization.h"
#include "PageWidgets.h"
#include "PlotWidget.h"
#include "ReleaseNotesDialog.h"
#include "Theme.h"
#include "services/FrameDiagnostics.h"
#include <QAction>
#include <QApplication>
#include <QAbstractItemView>
#include <QColorDialog>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QFileDialog>
#include <QFile>
#include <QFileInfo>
#include <QFormLayout>
#include <QFrame>
#include <QHBoxLayout>
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QMessageBox>
#include <QProcess>
#include <QProgressBar>
#include <QSettings>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace studio::ui {
namespace {
constexpr quintptr NativeMenuVersion = 0x1A10;
constexpr quintptr NativeMenuReleaseNotes = 0x1A11;
constexpr quintptr NativeMenuDefaultTheme = 0x1A12;
constexpr quintptr NativeMenuGraphiteTheme = 0x1A13;
constexpr quintptr NativeMenuForestTheme = 0x1A14;
constexpr quintptr NativeMenuPlumTheme = 0x1A15;
constexpr quintptr NativeMenuCustomTheme = 0x1A16;
constexpr quintptr NativeMenuLightTheme = 0x1A17;

QLabel* text(QString s, QString role = {}) {
    auto* l = new QLabel(i18n::text(s));
    l->setObjectName(role);
    return l;
}
QPushButton* action(QString s, QString role = {}) {
    auto* b = new QPushButton(i18n::text(s));
    b->setObjectName(role);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}
const QStringList titles{"条形码扫描", "运动调试", "编码器校准", "线性度检查", "角度误差",
                         "下线检查", "温度监测", "电机应答", "事件日志"};
QString resolvedRecordDirectory(const QString& supplied) {
    if (!supplied.trimmed().isEmpty())
        return supplied;
    QSettings settings;
    const auto defaultDirectory = QDir(QCoreApplication::applicationDirPath()).filePath("records");
    auto configured = settings.value("storage/recordDirectory", defaultDirectory).toString();
    QDir outputRoot(QCoreApplication::applicationDirPath());
    if (outputRoot.cdUp() && outputRoot.cdUp()) {
        const auto legacyDefault = QDir(outputRoot.filePath("MotorStudio/bin/records")).absolutePath();
        if (QDir(configured).absolutePath().compare(legacyDefault, Qt::CaseInsensitive) == 0) {
            configured = defaultDirectory;
            settings.setValue("storage/recordDirectory", configured);
        }
    }
    return configured;
}
QString operationName(DeviceOperation operation) {
    switch (operation) {
    case DeviceOperation::Calibration: return i18n::text("校准");
    case DeviceOperation::Inspection: return i18n::text("下线检查");
    case DeviceOperation::Firmware: return i18n::text("固件升级");
    case DeviceOperation::Parameters: return i18n::text("参数写入");
    case DeviceOperation::None: return {};
    }
    return {};
}
} // 匿名命名空间
MainWindow::MainWindow(QString directory, TransportKind transport, QWidget* parent)
    : QMainWindow(parent), device_(transport, nullptr),
      motionRecorder_(resolvedRecordDirectory(directory)),
      temperatureRecorder_(resolvedRecordDirectory(directory)) {
    setWindowTitle("DEEP Robotics · " + i18n::text("关节调试系统") + " · V" +
                   QCoreApplication::applicationVersion());
    resize(1366, 820);
    setMinimumSize(1100, 700);
    createAppearanceActions();
    QTimer::singleShot(0, this, &MainWindow::installNativeSystemMenu);
    auto* root = new QWidget;
    setCentralWidget(root);
    auto* horizontal = new QHBoxLayout(root);
    horizontal->setContentsMargins(0, 0, 0, 0);
    horizontal->setSpacing(0);
    auto* sidebar = new QWidget;
    sidebar->setObjectName("sidebar");
    sidebar->setFixedWidth(220);
    auto* nav = new QVBoxLayout(sidebar);
    nav->setContentsMargins(18, 12, 18, 14);
    nav->setSpacing(3);
    auto* sidebarToggleRow = new QHBoxLayout;
    sidebarToggleRow->setContentsMargins(0, 0, 0, 0);
    sidebarToggleRow->addStretch();
    auto* sidebarToggle = new QPushButton(QString::fromUtf8("‹"));
    sidebarToggle->setObjectName("sidebarToggle");
    sidebarToggle->setFixedSize(30, 28);
    sidebarToggle->setFocusPolicy(Qt::NoFocus);
    sidebarToggleRow->addWidget(sidebarToggle);
    nav->addLayout(sidebarToggleRow);
    auto* logo = new QLabel;
    logo->setObjectName("brandingLogo");
    const QPixmap logoPixmap(ThemeColors().light ? ":/resources/branding/deep-robotics-light.svg"
                                                : ":/resources/branding/deep-robotics.svg");
    logo->setPixmap(logoPixmap.scaled(184, 76, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    logo->setFixedSize(184, 76);
    logo->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    nav->addWidget(logo);
    nav->addSpacing(5);
    auto* workspaceLabel = text("工作空间", "muted");
    nav->addWidget(workspaceLabel);
    for (int i = 0; i < titles.size(); ++i) {
        auto* b = action(QString("%1    %2").arg(i + 1, 2, 10, QChar('0')).arg(i18n::text(titles[i])), "nav");
        b->setCheckable(true);
        navigation_.append(b);
        nav->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, i] { selectPage(i); });
    }
    nav->addStretch();
    auto* settingsLabel = text("设备与配置", "muted");
    nav->addWidget(settingsLabel);
    auto* manage = action("设备管理");
    manage->setObjectName("deviceManageButton");
    parameters_ = action("参数设置");
    parameters_->setObjectName("parametersButton");
    firmware_ = action("固件升级");
    storage_ = action("数据存储");
    storage_->setObjectName("storageSettingsButton");
    nav->addWidget(manage);
    nav->addWidget(parameters_);
    nav->addWidget(firmware_);
    nav->addWidget(storage_);
    nav->addSpacing(10);
    auto* protocolLabel = text("CAN FD  /  EtherCAN v1", "muted");
    protocolLabel->setObjectName("sidebarProtocol");
    nav->addWidget(protocolLabel);
    const QList<QWidget*> expandedOnly{logo, workspaceLabel, settingsLabel, manage, parameters_,
                                       firmware_, storage_, protocolLabel};
    const auto applySidebarState = [sidebar, nav, sidebarToggle, expandedOnly, this](bool collapsed) {
        sidebar->setProperty("collapsed", collapsed);
        sidebar->setFixedWidth(collapsed ? 68 : 220);
        nav->setContentsMargins(collapsed ? 8 : 18, 12, collapsed ? 8 : 18, 14);
        for (auto* widget : expandedOnly)
            widget->setVisible(!collapsed);
        for (int i = 0; i < navigation_.size(); ++i) {
            auto* button = navigation_[i];
            button->setText(collapsed
                                ? QString("%1").arg(i + 1, 2, 10, QChar('0'))
                                : QString("%1    %2")
                                      .arg(i + 1, 2, 10, QChar('0'))
                                      .arg(i18n::text(titles[i])));
            button->setToolTip(i18n::text(titles[i]));
            button->setProperty("compact", collapsed);
            button->style()->unpolish(button);
            button->style()->polish(button);
        }
        sidebarToggle->setText(collapsed ? QString::fromUtf8("›") : QString::fromUtf8("‹"));
        sidebarToggle->setToolTip(i18n::text(collapsed ? "展开侧栏" : "收起侧栏"));
        QSettings().setValue("ui/sidebarCollapsed", collapsed);
    };
    applySidebarState(QSettings().value("ui/sidebarCollapsed", false).toBool());
    connect(sidebarToggle, &QPushButton::clicked, this, [sidebar, applySidebarState] {
        applySidebarState(!sidebar->property("collapsed").toBool());
    });
    horizontal->addWidget(sidebar);
    auto* main = new QWidget;
    auto* vertical = new QVBoxLayout(main);
    vertical->setContentsMargins(20, 8, 20, 6);
    vertical->setSpacing(8);
    horizontal->addWidget(main, 1);
    auto* top = new QHBoxLayout;
    title_ = text({}, "title");
    top->addWidget(title_);
    top->addStretch();
    autoConnect_ = action(i18n::text("自动连接"));
    autoConnect_->setObjectName("autoConnectEntry");
    autoConnect_->setCheckable(true);
    autoConnect_->setFocusPolicy(Qt::NoFocus);
    autoConnect_->setToolTip(i18n::text("自动搜索并连接设备"));
    autoConnect_->setAccessibleName(i18n::text("自动连接"));
    top->addWidget(autoConnect_);
    refreshRate_ = text(i18n::text("刷新率") + QStringLiteral("：0 Hz"), "refreshRate");
    refreshRate_->setMinimumWidth(112);
    refreshRate_->setAlignment(Qt::AlignCenter);
    refreshRate_->setProperty("active", false);
    refreshRate_->setToolTip(i18n::text("最近一秒内收到并通过解析的运动控制应答帧数量"));
    top->addWidget(refreshRate_);
    connectionIdentity_ = text("● 未连接", "connectionIdentity");
    connectionIdentity_->setMinimumWidth(300);
    connectionIdentity_->setAlignment(Qt::AlignCenter);
    connectionIdentity_->setProperty("connectionState", "disconnected");
    top->addWidget(connectionIdentity_);
    auto* language = new QComboBox;
    language->setObjectName("languageSelector");
    language->setMinimumWidth(112);
    language->addItem("中文", "zh_CN");
    language->addItem("English", "en_US");
    language->setCurrentIndex(i18n::isEnglish() ? 1 : 0);
    language->setToolTip("语言 / Language");
    top->addWidget(language);
    connect(language, &QComboBox::currentIndexChanged, this, [language](int index) {
        const auto code = language->itemData(index).toString();
        if (code == i18n::languageCode())
            return;
        i18n::setLanguageCode(code);
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {}, QCoreApplication::applicationDirPath());
        qApp->quit();
    });
    top->addWidget(text("●  " + i18n::text("系统就绪"), "badge"));
    vertical->addLayout(top);
    auto* session = new QFrame;
    session->setObjectName("card");
    auto* bar = new QHBoxLayout(session);
    bar->setContentsMargins(14, 6, 14, 6);
    bar->addWidget(text("当前设备", "muted"));
    deviceCombo_ = new QComboBox;
    deviceCombo_->setMinimumWidth(260);
    deviceCombo_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    bar->addWidget(deviceCombo_, 1);
    connection_ = text("未连接", "muted");
    connection_->setObjectName("connectionStatus");
    bar->addWidget(connection_);
    connectButton_ = action("连接设备", "primary");
    bar->addWidget(connectButton_);
    auto* reset = action("复位");
    auto* home = action("设为零点");
    auto* clear = action("清除错误");
    bar->addWidget(home);
    bar->addWidget(reset);
    bar->addWidget(clear);
    vertical->addWidget(session);
    logs_ = new QPlainTextEdit;
    logs_->setReadOnly(true);
    logs_->setMaximumBlockCount(1000);
    logFlushTimer_.setSingleShot(true);
    logFlushTimer_.setInterval(50);
    connect(&logFlushTimer_, &QTimer::timeout, this, [this] {
        if (pendingLogLines_.isEmpty())
            return;
        constexpr int MaximumBatch = 200;
        const int count = qMin(MaximumBatch, pendingLogLines_.size());
        QStringList batch;
        batch.reserve(count);
        for (int index = 0; index < count; ++index)
            batch.append(pendingLogLines_.takeFirst());
        logs_->appendPlainText(batch.join('\n'));
        statusBar()->showMessage(pendingStatusMessage_, 12000);
        if (!pendingLogLines_.isEmpty())
            logFlushTimer_.start();
    });
    pages_ = new QStackedWidget;
    // 大型曲线和表单不能把固定的顶部栏挤出窗口；需要更多空间的页面自行滚动。
    pages_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    pages_->setMinimumHeight(0);
    PageContext c{&device_, &motionRecorder_, &temperatureRecorder_, &identity_, [this] { selectPage(1); }};
    pages_->addWidget(createIdentityPage(c));
    pages_->addWidget(createMotionPage(c));
    pages_->addWidget(createCalibrationPage(c));
    pages_->addWidget(createLinearityPage(c));
    pages_->addWidget(createAngleErrorPage(c));
    pages_->addWidget(createInspectionPage(c));
    pages_->addWidget(createTemperaturePage(c));
    pages_->addWidget(createResponsePage(c));
    pages_->addWidget(createEventLogPage(logs_, &motionRecorder_));
    vertical->addWidget(pages_, 1);
    auto* metrics = new QWidget;
    metrics->setObjectName("statusMetrics");
    auto* values = new QHBoxLayout(metrics);
    values->setContentsMargins(6, 0, 6, 0);
    values->setSpacing(14);
    QStringList names{"角度 / rad", "角速度 / rad/s", "扭矩 / N·m", "电机 / °C", "驱动器 / °C"};
    for (const auto& name : names) {
        auto* cellWidget = new QWidget;
        auto* cell = new QHBoxLayout(cellWidget);
        cell->setContentsMargins(0, 0, 0, 0);
        cell->setSpacing(6);
        cell->addWidget(text(i18n::text(name) + (i18n::isEnglish() ? ":" : "："),
                             "statusMetricName"));
        auto* value = text("—", "statusMetric");
        metrics_.append(value);
        cell->addWidget(value);
        values->addWidget(cellWidget);
    }
    statusWordPanel_ = new QFrame;
    statusWordPanel_->setObjectName("statusWordBanner");
    statusWordPanel_->setProperty("statusState", "disconnected");
    statusWordPanel_->setMinimumWidth(330);
    statusWordPanel_->setMaximumWidth(500);
    statusWordPanel_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    auto* statusWordLayout = new QHBoxLayout(statusWordPanel_);
    statusWordLayout->setContentsMargins(8, 2, 5, 2);
    statusWordLayout->setSpacing(6);
    auto* statusWordText = new QWidget;
    auto* statusWordTextLayout = new QVBoxLayout(statusWordText);
    statusWordTextLayout->setContentsMargins(0, 0, 0, 0);
    statusWordTextLayout->setSpacing(0);
    statusWordValue_ = text("STATUSWORD · 尚未连接设备", "statusWordValue");
    statusWordValue_->setObjectName("globalStatusWordValue");
    statusWordValue_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusWordTime_ = text("命令 23 · —", "statusWordTime");
    statusWordTime_->setObjectName("globalStatusWordTime");
    statusWordTime_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    statusWordTextLayout->addWidget(statusWordValue_);
    statusWordTextLayout->addWidget(statusWordTime_);
    statusWordLayout->addWidget(statusWordText, 1);
    statusWordClear_ = action("清除", "statusWordClear");
    statusWordClear_->setObjectName("statusWordClear");
    statusWordClear_->setToolTip(i18n::text("只清除本地状态字显示，不向设备发送命令"));
    statusWordClear_->setEnabled(false);
    statusWordLayout->addWidget(statusWordClear_);
    auto* versionWidget = new QWidget;
    auto* versionCell = new QHBoxLayout(versionWidget);
    versionCell->setContentsMargins(0, 0, 0, 0);
    versionCell->setSpacing(6);
    versionCell->addWidget(text(i18n::text("固件版本") + (i18n::isEnglish() ? ":" : "："),
                                "statusMetricName"));
    version_ = text("—", "statusMetric");
    versionCell->addWidget(version_);
    values->addWidget(versionWidget);
    values->addStretch(1);
    operation_ = text({}, "muted");
    operation_->setObjectName("statusOperation");
    operation_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    values->addWidget(operation_);
    values->addWidget(statusWordPanel_, 1);
    statusBar()->setSizeGripEnabled(false);
    statusBar()->setFixedHeight(42);
    // 设备指标和固件版本放在状态栏左侧，STATUSWORD 固定在最右侧。
    statusBar()->addPermanentWidget(metrics, 1);
    connect(manage, &QPushButton::clicked, this, &MainWindow::manageDevices);
    connect(autoConnect_, &QPushButton::clicked, this, &MainWindow::openAutoConnect);
    connect(parameters_, &QPushButton::clicked, this, [this] { showParameters(&device_, this); });
    connect(firmware_, &QPushButton::clicked, this, &MainWindow::firmwareDialog);
    connect(storage_, &QPushButton::clicked, this, &MainWindow::storageDialog);
    connect(home, &QPushButton::clicked, this, [this] { device_.command(Command::SetHome); });
    connect(reset, &QPushButton::clicked, this, [this] { device_.command(Command::Reset); });
    connect(clear, &QPushButton::clicked, this, [this] { device_.command(Command::ClearError); });
    connect(statusWordClear_, &QPushButton::clicked, this, [this] {
        hasStatusWordResponse_ = false;
        resetStatusWordDisplay(device_.connected(), true);
    });
    connect(connectButton_, &QPushButton::clicked, this, [this] {
        if (device_.connected() || device_.connecting())
            device_.disconnectDevice();
        else if (deviceCombo_->currentIndex() >= 0) {
            DeviceSession session{registry_.motors().at(deviceCombo_->currentIndex())};
            for (const auto& board : registry_.boards()) {
                if (board.ip == session.address.ip) {
                    session.dataBitrate = board.dataBitrate;
                    session.protocolId = board.protocolId;
                    break;
                }
            }
            device_.connectSession(session);
        }
    });
    connect(deviceCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        device_.disconnectDevice();
        motionRecorder_.stop();
        temperatureRecorder_.stop();
        if (deviceCombo_->currentIndex() >= 0) {
            QSettings settings;
            settings.setValue("device/lastSelection", deviceCombo_->currentText());
        }
    });
    connect(&device_, &AsyncDeviceService::stateChanged, this, &MainWindow::updateState);
    connect(&device_, &AsyncDeviceService::versionChanged, version_, &QLabel::setText);
    connect(&device_, &AsyncDeviceService::refreshRateChanged, this, [this](int rate) {
        refreshRate_->setText(i18n::text("刷新率") + QStringLiteral("：%1 Hz").arg(rate));
        refreshRate_->setProperty("active", rate > 0);
        refreshRate_->style()->unpolish(refreshRate_);
        refreshRate_->style()->polish(refreshRate_);
    });
    connect(&device_, &AsyncDeviceService::statusWordReceived, this, &MainWindow::showStatusWord);
    // AsyncDeviceService 已经跨过设备线程边界，无须再次排队转发日志。
    connect(&device_, &AsyncDeviceService::message, this, &MainWindow::log);
    metricRefreshTimer_.setSingleShot(true);
    metricRefreshTimer_.setInterval(50);
    connect(&metricRefreshTimer_, &QTimer::timeout, this, [this] {
        if (!metricRefreshPending_)
            return;
        metricRefreshPending_ = false;
        const float data[] = {latestTelemetry_.position, latestTelemetry_.velocity,
                              latestTelemetry_.torque, latestTelemetry_.motorTemperature,
                              latestTelemetry_.driverTemperature};
        for (int i = 0; i < 5; ++i)
            metrics_[i]->setText(QString::number(data[i], 'f', 2));
    });
    connect(&device_, &AsyncDeviceService::telemetryReceived, this, [this](Telemetry t) {
        // 遥测和 CSV 保留每个样本；状态栏文字合并刷新，避免 500 Hz 数据持续触发大量布局更新。
        latestTelemetry_ = t;
        metricRefreshPending_ = true;
        if (!metricRefreshTimer_.isActive())
            metricRefreshTimer_.start();
        temperatureRecorder_.append(t, {});
    });
    connect(&device_, &AsyncDeviceService::motionSampleReceived, this,
            [this](Telemetry feedback, double sampleSeconds, Motion target) {
        motionRecorder_.appendMotionSample(sampleSeconds, feedback, target);
    });
    // 只有点击“开始记录 CSV”才创建运动文件；运动结束时关闭已有记录。
    connect(&device_, &AsyncDeviceService::motionStateChanged, this, [this](bool active) {
        if (!active)
            motionRecorder_.stop();
    });
    for (auto* recorder : {&motionRecorder_, &temperatureRecorder_}) {
        connect(recorder, &RecordingService::errorOccurred, this, &MainWindow::log);
        connect(recorder, &RecordingService::changed, this, [this](bool active, QString path) {
            log(i18n::text(active ? "开始记录：" : "记录已保存：") + path);
        });
    }
    connect(&device_, &AsyncDeviceService::stateChanged, this, [this, home, reset, clear] {
        const bool ok = device_.connected();
        for (auto* b : {home, reset, clear})
            b->setEnabled(ok);
    });
    connect(&registry_, &DeviceRegistry::changed, this, [this] {
        device_.disconnectDevice();
        loadProfiles();
    });
    connect(&registry_, &DeviceRegistry::errorOccurred, this, &MainWindow::log);
    loadProfiles();
    selectPage(1);
    updateState();
    log("关节调试系统已启动");
}

void MainWindow::createAppearanceActions() {
    auto* releaseNotes = new QAction(i18n::text("更新记录…"), this);
    releaseNotes->setObjectName("releaseNotesAction");
    connect(releaseNotes, &QAction::triggered, this, &MainWindow::showReleaseNotes);

    const auto addTheme = [this](const QString& title, const QColor& color,
                                 const char* objectName) {
        auto* theme = new QAction(i18n::text(title), this);
        theme->setObjectName(objectName);
        theme->setCheckable(true);
        theme->setData(color.name());
        connect(theme, &QAction::triggered, this,
                [this, color] { applyBackgroundTheme(color); });
    };
    addTheme("默认深蓝", defaultBackground(), "backgroundDefaultAction");
    addTheme("浅色", QColor("#f1f4f8"), "backgroundLightAction");
    addTheme("石墨灰", QColor("#22252b"), "backgroundGraphiteAction");
    addTheme("深墨绿", QColor("#0b231f"), "backgroundForestAction");
    addTheme("深紫色", QColor("#21172a"), "backgroundPlumAction");

    auto* customTheme = new QAction(i18n::text("自定义颜色…"), this);
    customTheme->setObjectName("backgroundCustomAction");
    customTheme->setCheckable(true);
    connect(customTheme, &QAction::triggered, this, [this] {
        const QColor selected = QColorDialog::getColor(
            savedBackground(), this, i18n::text("选择背景颜色"));
        if (selected.isValid())
            applyBackgroundTheme(normalizedBackground(selected));
    });
}

void MainWindow::applyBackgroundTheme(const QColor& requestedColor) {
    const QColor color = normalizedBackground(requestedColor);
    QSettings().setValue("ui/backgroundColor", color.name());
    qApp->setPalette(themePalette(color));
    qApp->setStyleSheet(studio::ui::styleSheet(color));
    for (auto* plot : findChildren<PlotWidget*>())
        plot->refreshTheme();
    if (auto* logo = findChild<QLabel*>("brandingLogo")) {
        const QPixmap pixmap(ThemeColors(color).light ? ":/resources/branding/deep-robotics-light.svg"
                                                     : ":/resources/branding/deep-robotics.svg");
        logo->setPixmap(pixmap.scaled(184, 76, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    updateNativeThemeChecks();
}

void MainWindow::showReleaseNotes() {
    if (auto* existing = findChild<QDialog*>("releaseNotesDialog")) {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }

    createReleaseNotesDialog(this)->show();
}

void MainWindow::installNativeSystemMenu() {
#ifdef Q_OS_WIN
    if (nativeMenuInstalled_)
        return;
    auto* windowHandle = reinterpret_cast<HWND>(winId());
    HMENU systemMenu = GetSystemMenu(windowHandle, FALSE);
    if (!systemMenu)
        return;
    HMENU themeMenu = CreatePopupMenu();
    if (!themeMenu)
        return;

    const auto menuText = [](const QString& value) {
        return reinterpret_cast<LPCWSTR>(value.utf16());
    };
    const auto* defaultAction = findChild<QAction*>("backgroundDefaultAction");
    const auto* lightAction = findChild<QAction*>("backgroundLightAction");
    const auto* graphiteAction = findChild<QAction*>("backgroundGraphiteAction");
    const auto* forestAction = findChild<QAction*>("backgroundForestAction");
    const auto* plumAction = findChild<QAction*>("backgroundPlumAction");
    const auto* customAction = findChild<QAction*>("backgroundCustomAction");
    const auto* notesAction = findChild<QAction*>("releaseNotesAction");
    if (!defaultAction || !lightAction || !graphiteAction || !forestAction || !plumAction || !customAction ||
        !notesAction) {
        DestroyMenu(themeMenu);
        return;
    }

    const bool themeReady =
        AppendMenuW(themeMenu, MF_STRING, NativeMenuDefaultTheme,
                    menuText(defaultAction->text())) &&
        AppendMenuW(themeMenu, MF_STRING, NativeMenuLightTheme,
                    menuText(lightAction->text())) &&
        AppendMenuW(themeMenu, MF_STRING, NativeMenuGraphiteTheme,
                    menuText(graphiteAction->text())) &&
        AppendMenuW(themeMenu, MF_STRING, NativeMenuForestTheme,
                    menuText(forestAction->text())) &&
        AppendMenuW(themeMenu, MF_STRING, NativeMenuPlumTheme,
                    menuText(plumAction->text())) &&
        AppendMenuW(themeMenu, MF_SEPARATOR, 0, nullptr) &&
        AppendMenuW(themeMenu, MF_STRING, NativeMenuCustomTheme,
                    menuText(customAction->text()));
    if (!themeReady) {
        DestroyMenu(themeMenu);
        return;
    }

    const QString versionText = i18n::text("当前软件版本") + "  V" +
                                QCoreApplication::applicationVersion();
    const bool systemMenuReady =
        InsertMenuW(systemMenu, 0, MF_BYPOSITION | MF_STRING | MF_GRAYED, NativeMenuVersion,
                    menuText(versionText)) &&
        InsertMenuW(systemMenu, 1, MF_BYPOSITION | MF_STRING, NativeMenuReleaseNotes,
                    menuText(notesAction->text())) &&
        InsertMenuW(systemMenu, 2, MF_BYPOSITION | MF_POPUP,
                    reinterpret_cast<UINT_PTR>(themeMenu), menuText(i18n::text("背景颜色"))) &&
        InsertMenuW(systemMenu, 3, MF_BYPOSITION | MF_SEPARATOR, 0, nullptr);
    if (!systemMenuReady) {
        GetSystemMenu(windowHandle, TRUE);
        return;
    }

    nativeThemeMenu_ = reinterpret_cast<quintptr>(themeMenu);
    nativeMenuInstalled_ = true;
    updateNativeThemeChecks();
    DrawMenuBar(windowHandle);
#endif
}

void MainWindow::updateNativeThemeChecks() {
    const QColor current = savedBackground();
    const QList<QPair<const char*, quintptr>> themes{
        {"backgroundDefaultAction", NativeMenuDefaultTheme},
        {"backgroundLightAction", NativeMenuLightTheme},
        {"backgroundGraphiteAction", NativeMenuGraphiteTheme},
        {"backgroundForestAction", NativeMenuForestTheme},
        {"backgroundPlumAction", NativeMenuPlumTheme}};
    bool presetMatched = false;
    for (const auto& item : themes) {
        auto* theme = findChild<QAction*>(item.first);
        const bool selected = theme && QColor(theme->data().toString()) == current;
        if (theme)
            theme->setChecked(selected);
        presetMatched = presetMatched || selected;
#ifdef Q_OS_WIN
        if (nativeThemeMenu_)
            CheckMenuItem(reinterpret_cast<HMENU>(nativeThemeMenu_), UINT(item.second),
                          MF_BYCOMMAND | (selected ? MF_CHECKED : MF_UNCHECKED));
#endif
    }
    if (auto* custom = findChild<QAction*>("backgroundCustomAction"))
        custom->setChecked(!presetMatched);
#ifdef Q_OS_WIN
    if (nativeThemeMenu_)
        CheckMenuItem(reinterpret_cast<HMENU>(nativeThemeMenu_), NativeMenuCustomTheme,
                      MF_BYCOMMAND | (!presetMatched ? MF_CHECKED : MF_UNCHECKED));
#endif
}

bool MainWindow::nativeEvent(const QByteArray& eventType, void* message, qintptr* result) {
#ifdef Q_OS_WIN
    Q_UNUSED(eventType);
    auto* nativeMessage = static_cast<MSG*>(message);
    if (nativeMessage && nativeMessage->message == WM_SYSCOMMAND) {
        const UINT command = UINT(nativeMessage->wParam & 0xffffu);
        const char* actionName = nullptr;
        switch (command) {
        case NativeMenuReleaseNotes: actionName = "releaseNotesAction"; break;
        case NativeMenuDefaultTheme: actionName = "backgroundDefaultAction"; break;
        case NativeMenuLightTheme: actionName = "backgroundLightAction"; break;
        case NativeMenuGraphiteTheme: actionName = "backgroundGraphiteAction"; break;
        case NativeMenuForestTheme: actionName = "backgroundForestAction"; break;
        case NativeMenuPlumTheme: actionName = "backgroundPlumAction"; break;
        case NativeMenuCustomTheme: actionName = "backgroundCustomAction"; break;
        default: break;
        }
        if (actionName) {
            if (auto* selectedAction = findChild<QAction*>(actionName))
                selectedAction->trigger();
            if (result)
                *result = 0;
            return true;
        }
    }
#endif
    return QMainWindow::nativeEvent(eventType, message, result);
}

MainWindow::~MainWindow() {
    motionRecorder_.stop();
    temperatureRecorder_.stop();
    device_.disconnectDevice();
}
void MainWindow::selectPage(int i) {
    if (i < 0 || i >= pages_->count())
        return;
    pages_->setCurrentIndex(i);
    title_->setText(i18n::text(titles[i]));
    for (int n = 0; n < navigation_.size(); ++n)
        navigation_[n]->setChecked(n == i);
    if (i == 0) {
        QTimer::singleShot(0, this, [this] {
            const char* target = identity_.deviceId.trimmed().isEmpty() ? "barcodeDeviceId" : "barcodeSerial";
            if (auto* input = findChild<QLineEdit*>(target)) {
                input->setFocus(Qt::OtherFocusReason);
                input->selectAll();
            }
        });
    }
}
void MainWindow::log(QString message) {
    message = i18n::text(message);
    constexpr int MaximumPendingLogLines = 2000;
    if (pendingLogLines_.size() >= MaximumPendingLogLines)
        pendingLogLines_.removeFirst();
    pendingLogLines_.append(QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz") +
                            "  " + message);
    pendingStatusMessage_ = std::move(message);
    if (!logFlushTimer_.isActive())
        logFlushTimer_.start();
}
void MainWindow::updateState() {
    QString status;
    QString color;
    QString tooltip;
    QString identityState;
    QString identityText;
    const auto session = device_.session();
    const auto sessionText = QString("%1 · %2 Mbps · CAN %3 · ID %4")
                                 .arg(session.address.ip)
                                 .arg(session.dataBitrate / 1000000)
                                 .arg(session.address.channel)
                                 .arg(session.address.motor);
    switch (device_.connectionState()) {
    case AsyncDeviceService::ConnectionState::Disconnected:
        status = "● 未连接";
        color = "muted";
        tooltip = "尚未建立设备通信";
        identityState = "disconnected";
        identityText = "● 未连接";
        break;
    case AsyncDeviceService::ConnectionState::Connecting:
        status = "● 正在建立通信…";
        color = "warning";
        tooltip = "等待设备返回合法版本应答";
        identityState = "connecting";
        identityText = i18n::text("● 连接中") + " · " + sessionText;
        break;
    case AsyncDeviceService::ConnectionState::Connected:
        status = device_.enabled() ? "● 通信正常 / 已使能" : "● 通信正常 / 已失能";
        color = "success";
        tooltip = "已收到设备的合法版本应答";
        identityState = "connected";
        identityText = i18n::text("● 已连接") + " · " + sessionText;
        break;
    case AsyncDeviceService::ConnectionState::Failed:
        status = "● 通信失败";
        color = "danger";
        tooltip = "未收到设备应答，请检查 IP、网卡和设备供电";
        identityState = "failed";
        identityText = i18n::text("● 连接失败") + " · " + sessionText;
        break;
    }
    connection_->setText(i18n::text(status));
    widgets::setStatusTone(connection_, color.toUtf8().constData());
    connection_->setToolTip(i18n::text(tooltip));
    connectionIdentity_->setText(i18n::text(identityText));
    connectionIdentity_->setToolTip(i18n::text(tooltip));
    connectionIdentity_->setProperty("connectionState", identityState);
    connectionIdentity_->style()->unpolish(connectionIdentity_);
    connectionIdentity_->style()->polish(connectionIdentity_);
    connectButton_->setText(i18n::text(device_.connected() ? "断开连接"
                                                           : device_.connecting() ? "取消连接" : "连接设备"));
    parameters_->setEnabled(device_.connected() && !device_.busy());
    firmware_->setEnabled(device_.connected() && !device_.enabled() && !device_.busy());
    firmware_->setToolTip(i18n::text("升级前请确认电机已失能。升级过程中请保持设备连接并勿关闭软件。"));
    operation_->setText(device_.busy() ? i18n::text("当前任务") + " · " + operationName(device_.operation())
                                       : QString{});
    if (!device_.connected()) {
        hasStatusWordResponse_ = false;
        resetStatusWordDisplay(false);
    } else if (!hasStatusWordResponse_) {
        resetStatusWordDisplay(true);
    }
    if (!device_.connected()) {
        metricRefreshTimer_.stop();
        metricRefreshPending_ = false;
        for (auto* m : metrics_)
            m->setText("—");
        motionRecorder_.stop();
        temperatureRecorder_.stop();
    }
}

void MainWindow::resetStatusWordDisplay(bool connected, bool cleared) {
    if (!statusWordPanel_ || !statusWordValue_ || !statusWordTime_)
        return;
    statusWordPanel_->setProperty("statusState", connected ? "waiting" : "disconnected");
    const auto state = !connected ? i18n::text("尚未连接设备")
                                  : cleared ? i18n::text("显示已清除，等待新应答")
                                            : i18n::text("等待真实应答或设备主动上报");
    const auto value = QStringLiteral("STATUSWORD · ") + state;
    statusWordValue_->setText(value);
    statusWordValue_->setToolTip(value);
    statusWordTime_->setText(cleared && connected
                                 ? i18n::text("本地显示已清除") + QStringLiteral(" · ") +
                                       QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz")
                                 : i18n::text("命令 23") + QStringLiteral(" · ") +
                                       i18n::text("尚未收到"));
    statusWordTime_->setToolTip(statusWordTime_->text());
    if (statusWordClear_)
        statusWordClear_->setEnabled(false);
    statusWordPanel_->style()->unpolish(statusWordPanel_);
    statusWordPanel_->style()->polish(statusWordPanel_);
}

void MainWindow::showStatusWord(quint16 word) {
    if (!statusWordPanel_ || !statusWordValue_ || !statusWordTime_)
        return;
    hasStatusWordResponse_ = true;
    const auto resolve = [](const QString& source) { return i18n::text(source); };
    const auto faults = FrameDiagnostics::statusWordFaults(word, true, resolve);
    const auto raw = QStringLiteral("0x") +
                     QStringLiteral("%1").arg(word, 4, 16, QLatin1Char('0')).toUpper();
    const auto details = word == 0 ? i18n::text("状态正常") : faults.join(i18n::text("；"));
    const auto value = QStringLiteral("STATUSWORD · ") + details;
    const auto fullText = value + QStringLiteral(" · ") + i18n::text("原始状态字") +
                          QStringLiteral(" ") + raw;
    statusWordValue_->setText(value);
    statusWordValue_->setToolTip(fullText);
    statusWordTime_->setText(i18n::text("收到命令 23") + QStringLiteral(" · ") + raw +
                             QStringLiteral(" · ") +
                             QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz"));
    statusWordTime_->setToolTip(fullText);
    if (statusWordClear_)
        statusWordClear_->setEnabled(true);
    statusWordPanel_->setProperty("statusState", word == 0 ? "normal" : "fault");
    statusWordPanel_->style()->unpolish(statusWordPanel_);
    statusWordPanel_->style()->polish(statusWordPanel_);
}
void MainWindow::loadProfiles() {
    const auto previous = deviceCombo_->currentText();
    const auto saved = QSettings().value("device/lastSelection").toString();
    QSignalBlocker blocker(deviceCombo_);
    deviceCombo_->clear();
    for (const auto& a : registry_.motors())
        deviceCombo_->addItem(a.key());
    int index = deviceCombo_->findText(previous);
    if (index < 0)
        index = deviceCombo_->findText(saved);
    if (index < 0) {
        for (int i = 0; i < registry_.motors().size(); ++i) {
            if (!registry_.motors().at(i).ip.startsWith("127.")) {
                index = i;
                break;
            }
        }
    }
    if (index >= 0) {
        deviceCombo_->setCurrentIndex(index);
        QSettings settings;
        settings.setValue("device/lastSelection", deviceCombo_->currentText());
    }
    connectButton_->setEnabled(!registry_.motors().isEmpty());
}
void MainWindow::manageDevices() {
    autoConnect_->setChecked(false);
    if (auto* existing = findChild<QDialog*>("deviceManagerDialog")) {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }
    showDeviceManager(&registry_, this);
}
void MainWindow::openAutoConnect() {
    autoConnect_->setChecked(true);
    if (auto* existing = findChild<QDialog*>("autoConnectDialog")) {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }
    auto* dialog = showAutoConnectDialog(
        &registry_, [this] { device_.disconnectDevice(); },
        [this](const DeviceSession& session) {
            const int index = deviceCombo_->findText(session.address.key());
            if (index >= 0) {
                QSignalBlocker blocker(deviceCombo_);
                deviceCombo_->setCurrentIndex(index);
            }
            QSettings().setValue("device/lastSelection", session.address.key());
            log(QString("自动连接发现设备 · %1 · %2 Mbps")
                    .arg(session.address.key())
                    .arg(session.dataBitrate / 1000000));
            device_.connectSession(session);
        },
        this);
    connect(dialog, &QDialog::finished, this, [this] { autoConnect_->setChecked(false); });
}
void MainWindow::firmwareDialog() {
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(i18n::text("固件升级"));
    dialog->setMinimumWidth(550);
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);
    layout->addWidget(text("固件升级", "title"));
    auto* notice = text("升级前请确认电机已失能。升级过程中请保持设备连接并勿关闭软件。", "warning");
    notice->setWordWrap(true);
    layout->addWidget(notice);
    auto* path = new QLineEdit;
    path->setReadOnly(true);
    path->setPlaceholderText(i18n::text("选择 .bin 文件"));
    auto* browse = action("选择固件");
    layout->addWidget(widgets::row({path, browse}));
    auto* progress = new QProgressBar;
    progress->setRange(0, 100);
    progress->setValue(0);
    progress->setFormat("%p%");
    layout->addWidget(progress);
    auto* status = text("等待选择固件", "muted");
    status->setWordWrap(true);
    layout->addWidget(status);
    auto* start = action("开始升级", "primary");
    layout->addWidget(start);

    const auto refresh = [this, path, start] {
        start->setEnabled(!path->text().isEmpty() && device_.connected() && !device_.enabled() &&
                          !device_.busy());
    };
    connect(browse, &QPushButton::clicked, dialog, [dialog, path, status, refresh] {
        const auto selected = QFileDialog::getOpenFileName(dialog, i18n::text("选择固件"), {},
                                                            i18n::text("固件 (*.bin)"));
        if (!selected.isEmpty()) {
            path->setText(QDir::toNativeSeparators(selected));
            status->setText(i18n::text("固件已选择，等待开始"));
            refresh();
        }
    });
    connect(start, &QPushButton::clicked, dialog, [this, path] {
        device_.startFirmwareUpdate(path->text());
    });
    connect(&device_, &AsyncDeviceService::firmwareProgress, dialog, [progress](int value) {
        progress->setValue(value);
    });
    connect(&device_, &AsyncDeviceService::firmwareStatusChanged, dialog, [status](const QString& value) {
        status->setText(i18n::text(value));
    });
    connect(&device_, &AsyncDeviceService::stateChanged, dialog, refresh);
    refresh();
    dialog->setWindowModality(Qt::WindowModal);
    dialog->show();
}
bool MainWindow::setRecordingDirectory(const QString& directory) {
    if (motionRecorder_.isActive() || temperatureRecorder_.isActive()) {
        log(i18n::text("请先停止正在进行的记录，再修改保存目录"));
        return false;
    }
    if (!motionRecorder_.setDirectory(directory) || !temperatureRecorder_.setDirectory(directory))
        return false;
    QSettings settings;
    settings.setValue("storage/recordDirectory", motionRecorder_.directory());
    log(i18n::text("保存目录已更新：") + motionRecorder_.directory());
    return true;
}
void MainWindow::storageDialog() {
    auto* dialog = new QDialog(this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(i18n::text("数据存储"));
    dialog->setMinimumWidth(620);
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(14);
    layout->addWidget(text("数据存储", "title"));
    layout->addWidget(text("运动记录、温度记录、下线检查和事件日志统一使用此目录。", "muted"));
    auto* path = new QLineEdit(motionRecorder_.directory());
    path->setObjectName("recordDirectoryEdit");
    path->setPlaceholderText(i18n::text("输入保存目录"));
    layout->addWidget(path);
    auto* browse = action("浏览…");
    auto* open = action("打开当前目录");
    layout->addWidget(widgets::row({browse, open}));
    auto* apply = action("保存设置", "primary");
    layout->addWidget(apply);
    connect(browse, &QPushButton::clicked, dialog, [=, this] {
        const auto selected = QFileDialog::getExistingDirectory(dialog, i18n::text("选择保存目录"), path->text());
        if (!selected.isEmpty())
            path->setText(QDir::toNativeSeparators(selected));
    });
    connect(open, &QPushButton::clicked, dialog, [=, this] {
        QDir().mkpath(motionRecorder_.directory());
        QDesktopServices::openUrl(QUrl::fromLocalFile(motionRecorder_.directory()));
    });
    connect(apply, &QPushButton::clicked, dialog, [=, this] {
        if (setRecordingDirectory(path->text())) {
            path->setText(QDir::toNativeSeparators(motionRecorder_.directory()));
            dialog->accept();
        }
    });
    dialog->setWindowModality(Qt::WindowModal);
    dialog->show();
}
void MainWindow::startDemoMotion() {
    if (!device_.connected() && !registry_.motors().isEmpty())
        device_.connectDevice(registry_.motors().first());
    device_.setEnabled(true);
    MotionProgram p;
    p.waveform = Waveform::PositionSine;
    p.amplitude = 1;
    p.frequency = 0.2;
    p.target.kp = 10;
    p.target.kd = 0.5;
    device_.startMotion(p);
    device_.startLinearity();
}
void MainWindow::startLoopbackDemo() {
    const DeviceAddress loopback{"127.0.0.1", 0, 1};
    {
        QSignalBlocker blocker(deviceCombo_);
        deviceCombo_->insertItem(0, loopback.key());
        deviceCombo_->setCurrentIndex(0);
    }
    auto stage = std::make_shared<int>(0);
    auto connection = std::make_shared<QMetaObject::Connection>();
    *connection = connect(&device_, &AsyncDeviceService::stateChanged, this, [this, stage, connection] {
        if (*stage == 0 && device_.connected()) {
            *stage = 1;
            device_.setEnabled(true);
        } else if (*stage == 1 && device_.enabled()) {
            *stage = 2;
            MotionProgram program;
            program.waveform = Waveform::PositionSine;
            program.amplitude = 1;
            program.frequency = 0.2;
            program.target.kp = 10;
            program.target.kd = 0.5;
            device_.startMotion(program);
            disconnect(*connection);
        }
    });
    device_.connectSession({loopback, 5000000, "deep.ethercan.canfd.v1"});
}
void MainWindow::capturePages(const QString& directory) {
    QDir().mkpath(directory);
    for (int i = 0; i < pages_->count(); ++i) {
        selectPage(i);
        QApplication::processEvents();
        grab().save(directory + QString("/page-%1.png").arg(i + 1));
    }
    selectPage(1);
}
bool MainWindow::captureDeviceDialog(const QString& path) {
    manageDevices();
    QApplication::processEvents();
    bool managerSaved = false;
    for (auto* widget : QApplication::topLevelWidgets()) {
        if (auto* dialog = qobject_cast<QDialog*>(widget);
            dialog && dialog->windowTitle() == i18n::text("通信板与电机管理")) {
            const bool boardSaved = dialog->grab().save(path);
            auto* tabs = dialog->findChild<QTabWidget*>();
            bool popupSaved = false;
            if (auto* combo = dialog->findChild<QComboBox*>("boardProtocolCombo")) {
                combo->showPopup();
                QApplication::processEvents();
                const QFileInfo file(path);
                const auto popupPath = file.dir().filePath(file.completeBaseName() + "-popup." + file.suffix());
                popupSaved = combo->view()->window()->grab().save(popupPath);
                combo->hidePopup();
            }
            bool motorSaved = false;
            if (tabs && tabs->count() >= 2) {
                tabs->setCurrentIndex(1);
                QApplication::processEvents();
                const QFileInfo file(path);
                const auto motorPath =
                    file.dir().filePath(file.completeBaseName() + "-motor." + file.suffix());
                motorSaved = dialog->grab().save(motorPath);
            }
            managerSaved = boardSaved && popupSaved && motorSaved;
            dialog->close();
            QApplication::processEvents();
            break;
        }
    }
    if (!managerSaved)
        return false;

    openAutoConnect();
    QApplication::processEvents();
    for (auto* widget : QApplication::topLevelWidgets()) {
        if (auto* dialog = qobject_cast<QDialog*>(widget);
            dialog && dialog->objectName() == "autoConnectDialog") {
            const QFileInfo file(path);
            const auto autoPath =
                file.dir().filePath(file.completeBaseName() + "-auto-connect." + file.suffix());
            const bool saved = dialog->grab().save(autoPath);
            dialog->close();
            QApplication::processEvents();
            return saved;
        }
    }
    return false;
}
bool MainWindow::captureAuxiliaryDialogs(const QString& directory) {
    QDir().mkpath(directory);
    const auto findDialog = [](const QString& title) -> QDialog* {
        for (auto* widget : QApplication::topLevelWidgets())
            if (auto* dialog = qobject_cast<QDialog*>(widget); dialog && dialog->windowTitle() == title)
                return dialog;
        return nullptr;
    };
    auto saveAndClose = [&](const QString& title, const QString& name) {
        QApplication::processEvents();
        auto* dialog = findDialog(title);
        if (!dialog)
            return false;
        const bool saved = dialog->grab().save(QDir(directory).filePath(name));
        dialog->close();
        QApplication::processEvents();
        return saved;
    };

    showParameters(&device_, this);
    QApplication::processEvents();
    bool parametersSaved = false;
    if (auto* dialog = findDialog(i18n::text("电机参数"))) {
        const bool motorSaved = dialog->grab().save(QDir(directory).filePath("parameters.png"));
        bool protectionSaved = false;
        if (auto* tabs = dialog->findChild<QTabWidget*>("parameterTabs")) {
            tabs->setCurrentIndex(1);
            QApplication::processEvents();
            protectionSaved = dialog->grab().save(QDir(directory).filePath("parameters-protection.png"));
        }
        parametersSaved = motorSaved && protectionSaved;
        dialog->close();
        QApplication::processEvents();
    }
    firmwareDialog();
    const bool firmwareSaved = saveAndClose(i18n::text("固件升级"), "firmware.png");
    storageDialog();
    const bool storageSaved = saveAndClose(i18n::text("数据存储"), "storage.png");

    selectPage(1);
    QApplication::processEvents();
    auto* follow = findChild<QPushButton*>("curveFollowButton");
    bool curveSaved = false;
    if (follow) {
        follow->click();
        QApplication::processEvents();
        if (auto* dialog = findDialog(i18n::text("曲线跟随"))) {
            curveSaved = dialog->grab().save(QDir(directory).filePath("curve-follow.png"));
            dialog->close();
            QApplication::processEvents();
        }
    }
    return parametersSaved && firmwareSaved && storageSaved && curveSaved;
}
} // 命名空间 studio::ui
