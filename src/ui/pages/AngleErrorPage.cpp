#include "ui/PageWidgets.h"
#include <QTimer>
#include <array>
#include <optional>

namespace studio::ui {
using namespace widgets;

QWidget* createAngleErrorPage(PageContext c) {
    auto* page = new QWidget;
    auto* root = new QHBoxLayout(page);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(16);

    auto* readings = new QWidget;
    auto* grid = new QGridLayout(readings);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setHorizontalSpacing(14);
    grid->setVerticalSpacing(14);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);
    grid->setRowStretch(0, 1);
    grid->setRowStretch(1, 1);

    std::array<QLabel*, 4> values{};
    const std::array<QString, 4> names{"参考角度", "关节端角度", "角度误差", "角度误差"};
    const std::array<QString, 4> units{"rad", "rad", "rad", "°"};
    const std::array<const char*, 4> objectNames{"referenceAngleValue", "jointAngleValue",
                                                 "angleErrorRadiansValue", "angleErrorDegreesValue"};
    for (int index = 0; index < 4; ++index) {
        QVBoxLayout* content;
        auto* panel = card({}, content);
        content->setContentsMargins(24, 22, 24, 22);
        content->addWidget(label(names[index], "section"));
        values[index] = label("—", "angleReading");
        values[index]->setObjectName(objectNames[index]);
        values[index]->setProperty("angleReading", true);
        values[index]->setAlignment(Qt::AlignCenter);
        content->addStretch();
        content->addWidget(values[index]);
        auto* unit = label(units[index], "muted");
        unit->setAlignment(Qt::AlignCenter);
        content->addWidget(unit);
        content->addStretch();
        grid->addWidget(panel, index / 2, index % 2);
    }
    root->addWidget(readings, 1);

    auto* controls = new QWidget;
    auto* right = new QVBoxLayout(controls);
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(12);
    right->addWidget(liveStatus(c.device, page));
    QVBoxLayout* acquisition;
    right->addWidget(card("角度误差读取", acquisition));
    auto* start = button("开始发送", "primary");
    start->setObjectName("startAngleErrorQuery");
    auto* stop = button("停止发送");
    stop->setObjectName("stopAngleErrorQuery");
    acquisition->addWidget(row({start, stop}));
    auto* status = label("未开始", "badge");
    status->setObjectName("angleErrorStatus");
    acquisition->addWidget(status);
    right->addStretch();
    root->addWidget(scrollPanel(controls));

    auto latest = std::make_shared<std::optional<AngleErrorData>>();
    auto* displayTimer = new QTimer(page);
    displayTimer->setSingleShot(true);
    displayTimer->setInterval(20);
    QObject::connect(displayTimer, &QTimer::timeout, page, [=] {
        if (!*latest)
            return;
        const auto data = **latest;
        values[0]->setText(QString::number(data.referenceAngle, 'f', 6));
        values[1]->setText(QString::number(data.jointAngle, 'f', 6));
        values[2]->setText(QString::number(data.errorRadians, 'f', 6));
        values[3]->setText(QString::number(data.errorDegrees, 'f', 4));
    });
    auto acceptReplies = std::make_shared<bool>(false);
    auto waiting = std::make_shared<bool>(false);
    auto* replyTimeout = new QTimer(page);
    replyTimeout->setSingleShot(true);
    replyTimeout->setInterval(1500);
    QObject::connect(start, &QPushButton::clicked, page, [=] {
        *acceptReplies = true;
        *waiting = true;
        status->setText(i18n::text("命令已发送，等待设备应答…"));
        replyTimeout->start();
        c.device->startAngleErrorQuery();
    });
    QObject::connect(stop, &QPushButton::clicked, page, [=] {
        *acceptReplies = false;
        *waiting = false;
        replyTimeout->stop();
        status->setText(i18n::text("已停止"));
        c.device->stopAngleErrorQuery();
    });
    QObject::connect(c.device, &AsyncDeviceService::angleErrorReceived, page,
                     [=](const AngleErrorData& data) {
        if (!*acceptReplies)
            return;
        *latest = data;
        if (!displayTimer->isActive())
            displayTimer->start();
        if (*waiting) {
            *waiting = false;
            replyTimeout->stop();
            status->setText(i18n::text("已收到设备应答"));
        }
    });
    QObject::connect(replyTimeout, &QTimer::timeout, page, [=] {
        if (*waiting)
            status->setText(i18n::text("未收到设备应答（仍在读取）"));
    });
    const auto refresh = [=](bool) {
        const bool active = c.device->angleErrorQueryActive();
        start->setEnabled(c.device->connected() && !c.device->busy() && !active);
        stop->setEnabled(active);
        if (!active) {
            *acceptReplies = false;
            *waiting = false;
            replyTimeout->stop();
            status->setText(i18n::text("已停止"));
        }
    };
    QObject::connect(c.device, &AsyncDeviceService::angleErrorQueryStateChanged, page, refresh);
    QObject::connect(c.device, &AsyncDeviceService::stateChanged, page, [=] { refresh(false); });
    refresh(false);
    return page;
}

} // 命名空间 studio::ui
