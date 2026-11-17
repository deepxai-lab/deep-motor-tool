#pragma once
#include "Pages.h"
#include "PlotWidget.h"
#include "Localization.h"
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QStyle>
#include <QTime>
#include <QUrl>
#include <QVBoxLayout>
#include <memory>

namespace studio::ui::widgets {
inline void setStatusTone(QWidget* widget, const char* tone) {
    widget->setProperty("statusTone", tone);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}
inline QLabel* label(QString text, QString role = {}) {
    auto* w = new QLabel(i18n::text(text));
    w->setObjectName(role);
    w->setWordWrap(true);
    return w;
}
inline QPushButton* button(QString text, QString role = {}) {
    auto* w = new QPushButton(i18n::text(text));
    w->setObjectName(role);
    w->setCursor(Qt::PointingHandCursor);
    return w;
}
inline QFrame* card(QString title, QVBoxLayout*& layout) {
    auto* w = new QFrame;
    w->setObjectName("card");
    layout = new QVBoxLayout(w);
    layout->setContentsMargins(18, 16, 18, 16);
    layout->setSpacing(12);
    if (!title.isEmpty())
        layout->addWidget(label(title, "section"));
    return w;
}
inline QScrollArea* scrollPanel(QWidget* content, int width = 330) {
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setMinimumWidth(width);
    scroll->setMaximumWidth(width + 18);
    content->setMinimumWidth(width - 22);
    scroll->setWidget(content);
    return scroll;
}
inline QDoubleSpinBox* spin(double min, double max, double value, int decimals = 2) {
    auto* w = new QDoubleSpinBox;
    w->setRange(min, max);
    w->setDecimals(decimals);
    w->setValue(value);
    w->setSingleStep(decimals ? 0.1 : 1);
    w->setButtonSymbols(QAbstractSpinBox::PlusMinus);
    w->setMinimumWidth(110);
    return w;
}
inline QWidget* row(std::initializer_list<QWidget*> widgets) {
    auto* w = new QWidget;
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(8);
    for (auto* v : widgets)
        h->addWidget(v);
    return w;
}
inline void enableButton(QPushButton* b, AsyncDeviceService* d, QWidget* page, bool needsEnabled = false) {
    const auto refresh = [=] {
        b->setEnabled(d->connected() && !d->busy() && (!needsEnabled || d->enabled()));
    };
    QObject::connect(d, &AsyncDeviceService::stateChanged, page, refresh);
    refresh();
}
inline QPushButton* motorEnable(AsyncDeviceService* d, QWidget* page) {
    auto* b = button("使能电机", "primary");
    b->setProperty("alwaysAvailableAction", true);
    QObject::connect(b, &QPushButton::clicked, page, [d] { d->setEnabled(!d->enabled()); });
    auto refresh = [=] {
        b->setText(i18n::text(d->enabled() ? "失能电机" : "使能电机"));
        // 与老上位机一致，控件始终保持可见、可点击。
        // 是否允许发送仍由 DeviceService 负责校验。
        b->setEnabled(true);
    };
    QObject::connect(d, &AsyncDeviceService::stateChanged, page, refresh);
    refresh();
    return b;
}
inline QFrame* liveStatus(AsyncDeviceService* d, QWidget* page) {
    auto* panel = new QFrame;
    panel->setObjectName("statusPanel");
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(14, 12, 14, 12);
    layout->setSpacing(4);
    layout->addWidget(label("实时状态", "section"));
    auto* time = label("--:--:--", "statusTime");
    auto* status = label("等待连接设备", "liveStatus");
    layout->addWidget(time);
    layout->addWidget(status);

    const auto show = [=](const QString& text) {
        time->setText(QTime::currentTime().toString("HH:mm:ss"));
        status->setText(i18n::text(text));
    };
    const auto refresh = [=] {
        if (!d->connected())
            show("等待连接设备");
        else if (d->operation() == DeviceOperation::Calibration)
            show("校准进行中");
        else if (d->operation() == DeviceOperation::Inspection)
            show("下线检查进行中");
        else if (d->operation() == DeviceOperation::Firmware)
            show("固件升级进行中");
        else
            show(d->enabled() ? "电机已使能" : "电机已连接，当前失能");
    };
    QObject::connect(d, &AsyncDeviceService::stateChanged, panel, refresh);
    QObject::connect(d, &AsyncDeviceService::message, panel, show);
    refresh();
    Q_UNUSED(page);
    return panel;
}
} // 命名空间 studio::ui::widgets
