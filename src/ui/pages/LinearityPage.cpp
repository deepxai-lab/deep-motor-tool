#include "ui/PageWidgets.h"

namespace studio::ui {
using namespace widgets;

namespace {
class FlexibleScrollArea final : public QScrollArea {
  public:
    QSize minimumSizeHint() const override { return QSize(0, 0); }
};
} // 匿名命名空间

QWidget* createLinearityPage(PageContext c) {
    auto* page = new FlexibleScrollArea;
    page->setWidgetResizable(true);
    page->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    page->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    page->setMinimumSize(0, 0);
    page->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    page->setFrameShape(QFrame::NoFrame);
    auto* content = new QWidget;
    content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    content->setMinimumSize(0, 0);
    auto* h = new QHBoxLayout(content);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(16);
    auto* plot = new PlotWidget("输出轴编码器角度", "角度 / rad");
    plot->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Ignored);
    plot->setMinimumHeight(0);
    // 已部署程序保留 30 s 数据，右侧预留 3 s，并在接收值下方、上方
    // 分别使用 0.5 和 3 的非对称 Y 轴边距。
    plot->setRollingWindow(30.0, 3.0, 0.5, 3.0);
    h->addWidget(plot, 1);
    QVBoxLayout* tools;
    auto* pane = card("线性度检查", tools);
    auto* toolPanel = scrollPanel(pane);
    toolPanel->setSizeAdjustPolicy(QAbstractScrollArea::AdjustIgnored);
    toolPanel->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Ignored);
    toolPanel->setMinimumHeight(0);
    h->addWidget(toolPanel);
    auto* start = button("发送参数", "primary");
    auto* stop = button("停止");
    auto* clear = button("清除波形");
    auto* clearError = button("清除错误");
    tools->addWidget(start);
    tools->addWidget(stop);
    tools->addWidget(clear);
    tools->addWidget(clearError);
    auto* statusPanel = new QFrame;
    statusPanel->setObjectName("statusPanel");
    auto* statusLayout = new QVBoxLayout(statusPanel);
    statusLayout->setContentsMargins(14, 12, 14, 12);
    statusLayout->setSpacing(4);
    statusLayout->addWidget(label("实时状态", "section"));
    auto* time = label("--:--:--", "statusTime");
    auto* status = label("线性度检查未开始", "liveStatus");
    statusLayout->addWidget(time);
    statusLayout->addWidget(status);
    tools->addWidget(statusPanel);
    tools->addStretch();
    auto* enable = button("电机使能", "primary");
    tools->addWidget(enable);
    QObject::connect(enable, &QPushButton::clicked, page,
                     [=] { c.device->setEnabled(!c.device->enabled()); });
    QObject::connect(start, &QPushButton::clicked, c.device, &AsyncDeviceService::startLinearity);
    QObject::connect(stop, &QPushButton::clicked, c.device, &AsyncDeviceService::stopLinearity);
    QObject::connect(clear, &QPushButton::clicked, plot, &PlotWidget::clear);
    QObject::connect(clearError, &QPushButton::clicked, page,
                     [=] { c.device->command(Command::ClearError); });
    QObject::connect(c.device, &AsyncDeviceService::encoderPoint, plot,
                     [=](double x, double y) { plot->append(x, y); });
    QObject::connect(c.device, &AsyncDeviceService::linearityStateChanged, page, [=](bool active) {
        time->setText(QTime::currentTime().toString("HH:mm:ss"));
        status->setText(i18n::text(active ? "线性度检查开始" : "线性度检查停止"));
    });
    const auto refresh = [=] {
        const bool connected = c.device->connected();
        const bool active = c.device->linearityActive();
        start->setEnabled(connected && c.device->enabled() && !c.device->busy() && !active);
        stop->setEnabled(connected && active);
        clearError->setEnabled(connected && !c.device->busy());
        // 与已部署程序一致，电机控制始终可用。
        // 服务层校验请求的命令，并可在不虚构协议取消帧的情况下
        // 释放失效的校准等待状态。
        enable->setEnabled(true);
        enable->setText(i18n::text(c.device->enabled() ? "电机失能" : "电机使能"));
        if (!connected) {
            time->setText("--:--:--");
            status->setText(i18n::text("未连接"));
        } else if (!active && status->text() == i18n::text("未连接")) {
            status->setText(i18n::text("线性度检查未开始"));
        }
    };
    QObject::connect(c.device, &AsyncDeviceService::stateChanged, page, refresh);
    QObject::connect(c.device, &AsyncDeviceService::linearityStateChanged, page, refresh);
    refresh();
    page->setWidget(content);
    return page;
}
} // 命名空间 studio::ui
