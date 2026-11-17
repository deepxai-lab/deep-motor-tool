#include "AutoConnectDialog.h"
#include "PageWidgets.h"
#include "services/DeviceDiscovery.h"
#include <QHostAddress>
#include <QPointer>
#include <QSettings>
#include <QSpinBox>
#include <QTimer>

namespace studio::ui {
using namespace widgets;

QDialog* showAutoConnectDialog(DeviceRegistry* registry, std::function<void()> beforeScan,
                               std::function<void(const DeviceSession&)> connectDiscovered,
                               QWidget* parent) {
    auto* dialog = new QDialog(parent);
    dialog->setObjectName("autoConnectDialog");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(i18n::text("自动连接设备"));
    dialog->setMinimumSize(580, 470);
    dialog->resize(620, 500);
    dialog->setWindowModality(Qt::WindowModal);

    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(26, 24, 26, 24);
    layout->setSpacing(16);
    layout->addWidget(label("自动连接", "title"));
    layout->addWidget(
        label("输入通信板 IP 和 CAN 通道，程序将依次搜索 5 Mbps、8 Mbps 下的电机 ID 0–15。",
              "muted"));

    auto* inputPanel = new QFrame;
    inputPanel->setObjectName("card");
    auto* inputLayout = new QVBoxLayout(inputPanel);
    inputLayout->setContentsMargins(18, 16, 18, 16);
    inputLayout->setSpacing(12);
    auto* discoveryIp = new QLineEdit;
    discoveryIp->setObjectName("autoConnectIp");
    discoveryIp->setText(QSettings().value("discovery/ip", "192.168.1.121").toString());
    auto* discoveryChannel = new QSpinBox;
    discoveryChannel->setObjectName("autoConnectChannel");
    discoveryChannel->setRange(0, 3);
    discoveryChannel->setValue(QSettings().value("discovery/channel", 0).toInt());
    auto* form = new QFormLayout;
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(12);
    form->addRow(i18n::text("IPv4 地址"), discoveryIp);
    form->addRow(i18n::text("CAN 通道"), discoveryChannel);
    inputLayout->addLayout(form);
    layout->addWidget(inputPanel);

    auto* progress = new QProgressBar;
    progress->setObjectName("autoConnectProgress");
    progress->setRange(0, 32);
    progress->setValue(0);
    progress->setFormat("0 / 32");
    layout->addWidget(progress);

    auto* scanPanel = new QFrame;
    scanPanel->setObjectName("scanPanel");
    auto* scanLayout = new QVBoxLayout(scanPanel);
    scanLayout->setContentsMargins(16, 14, 16, 14);
    scanLayout->setSpacing(5);
    auto* pulse = label("等待开始", "scanPulse");
    auto* attempt = label("搜索顺序：5 Mbps / ID 0–15 → 8 Mbps / ID 0–15", "muted");
    auto* result = label("尚未检索", "scanResult");
    result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    scanLayout->addWidget(pulse);
    scanLayout->addWidget(attempt);
    scanLayout->addSpacing(4);
    scanLayout->addWidget(result);
    layout->addWidget(scanPanel, 1);

    auto* start = button("开始自动搜索", "primary");
    start->setObjectName("startAutoConnect");
    auto* stop = button("停止搜索");
    stop->setObjectName("stopAutoConnect");
    stop->setEnabled(false);
    layout->addWidget(row({start, stop}));

    auto* scanner = new DeviceDiscovery(dialog);
    auto* animation = new QTimer(dialog);
    animation->setInterval(260);
    auto animationPhase = std::make_shared<int>(0);
    auto found = std::make_shared<bool>(false);
    const auto setSearching = [=](bool searching) {
        discoveryIp->setEnabled(!searching);
        discoveryChannel->setEnabled(!searching);
        start->setEnabled(!searching);
        stop->setEnabled(searching);
        if (!searching)
            animation->stop();
    };

    QObject::connect(animation, &QTimer::timeout, dialog, [=] {
        *animationPhase = (*animationPhase + 1) % 4;
        pulse->setText(i18n::text("正在搜索") + QString(*animationPhase, QChar(0x00b7)));
    });
    QObject::connect(start, &QPushButton::clicked, dialog, [=] {
        const QHostAddress address(discoveryIp->text().trimmed());
        if (address.protocol() != QAbstractSocket::IPv4Protocol) {
            pulse->setText(i18n::text("输入有误"));
            result->setText(i18n::text("请输入有效 IPv4 地址和 CAN 通道"));
            setStatusTone(result, "danger");
            return;
        }
        if (beforeScan)
            beforeScan();
        QSettings settings;
        settings.setValue("discovery/ip", address.toString());
        settings.setValue("discovery/channel", discoveryChannel->value());
        *found = false;
        *animationPhase = 0;
        setStatusTone(result, "");
        scanner->start(address.toString(), discoveryChannel->value());
    });
    QObject::connect(stop, &QPushButton::clicked, scanner, &DeviceDiscovery::stop);
    QObject::connect(dialog, &QDialog::finished, scanner, &DeviceDiscovery::stop);
    QObject::connect(scanner, &DeviceDiscovery::scanStarted, dialog, [=](int total) {
        setSearching(true);
        progress->setRange(0, total);
        progress->setValue(0);
        progress->setFormat(QString("0 / %1").arg(total));
        pulse->setText(i18n::text("正在搜索"));
        result->setText(i18n::text("等待设备版本应答…"));
        animation->start();
    });
    QObject::connect(scanner, &DeviceDiscovery::attemptStarted, dialog,
                     [=](int current, int total, const DeviceSession& session) {
        progress->setValue(current);
        progress->setFormat(QString("%1 / %2").arg(current).arg(total));
        attempt->setText(QString("%1 · %2 Mbps · CAN %3 · ID %4")
                             .arg(session.address.ip)
                             .arg(session.dataBitrate / 1000000)
                             .arg(session.address.channel)
                             .arg(session.address.motor));
    });
    QObject::connect(scanner, &DeviceDiscovery::deviceFound, dialog,
                     [=](const DeviceSession& session, const QString& version) {
        *found = true;
        setSearching(false);
        pulse->setText(i18n::text("已发现设备，正在连接…"));
        setStatusTone(result, "success");
        result->setText(
            QString("IP：%1    波特率：%2 Mbps\nCAN 通道：%3    电机 ID：%4    固件版本：%5")
                .arg(session.address.ip)
                .arg(session.dataBitrate / 1000000)
                .arg(session.address.channel)
                .arg(session.address.motor)
                .arg(version.isEmpty() ? QString::fromUtf8("—") : version));
        if (!registry->storeDiscoveredSession(session)) {
            pulse->setText(i18n::text("自动连接结果无效，未保存设备"));
            setStatusTone(result, "danger");
            start->setEnabled(true);
            return;
        }
        if (connectDiscovered)
            connectDiscovered(session);
        start->setEnabled(false);
        stop->setEnabled(false);
        QPointer<QDialog> guard(dialog);
        QTimer::singleShot(1100, dialog, [guard] {
            if (guard)
                guard->accept();
        });
    });
    QObject::connect(scanner, &DeviceDiscovery::scanFinished, dialog, [=](int attempted) {
        setSearching(false);
        if (*found)
            return;
        pulse->setText(i18n::text("检索完成"));
        setStatusTone(result, "warning");
        result->setText(
            i18n::text("未发现可应答电机。已检查 5 Mbps、8 Mbps 和 ID 0–15，共 %1 项。")
                .arg(attempted));
    });
    QObject::connect(scanner, &DeviceDiscovery::scanStopped, dialog, [=](int attempted) {
        setSearching(false);
        pulse->setText(i18n::text("搜索已停止"));
        setStatusTone(result, "warning");
        result->setText(i18n::text("已完成 %1 / 32 项").arg(attempted));
    });
    QObject::connect(scanner, &DeviceDiscovery::validationFailed, dialog,
                     [=](const QString& reason) {
        setSearching(false);
        pulse->setText(i18n::text("输入有误"));
        setStatusTone(result, "danger");
        result->setText(i18n::text(reason));
    });

    dialog->show();
    return dialog;
}

} // 命名空间 studio::ui
