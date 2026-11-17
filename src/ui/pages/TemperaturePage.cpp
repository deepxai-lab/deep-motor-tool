#include "ui/PageWidgets.h"

namespace studio::ui {
using namespace widgets;

QWidget* createTemperaturePage(PageContext c) {
    auto* page = new QWidget;
    auto* h = new QHBoxLayout(page);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(16);
    auto* plots = new QSplitter(Qt::Vertical);
    plots->setChildrenCollapsible(false);
    auto* motor = new PlotWidget("电机温度", "温度 / °C");
    auto* driver = new PlotWidget("驱动器温度", "温度 / °C");
    // 遥测可能按 2 ms 运动节拍到达。限制屏幕历史长度，避免长时间记录使
    // 重绘逐渐变慢；RecordingService 仍会把每个收到的样本写入 CSV。
    motor->setRollingWindow(30.0, 3.0, 0.5);
    driver->setRollingWindow(30.0, 3.0, 0.5);
    plots->addWidget(motor);
    plots->addWidget(driver);
    plots->setStretchFactor(0, 1);
    plots->setStretchFactor(1, 1);
    h->addWidget(plots, 1);
    QVBoxLayout* tools;
    auto* pane = card("温度记录", tools);
    h->addWidget(scrollPanel(pane));
    tools->addWidget(label("显示最近 30 秒数据。绘制与文件记录分别控制。", "muted"));
    auto* draw = button("开始绘制", "primary");
    auto* record = button("开始温度记录", "primary");
    auto* clear = button("清空波形");
    clear->setObjectName("temperatureClearButton");
    auto* open = button("打开记录目录");
    tools->addWidget(draw);
    tools->addWidget(record);
    tools->addWidget(row({clear, open}));
    auto* drawStatus = label("绘制未开始", "badge");
    drawStatus->setObjectName("temperatureDrawStatus");
    tools->addWidget(drawStatus);
    auto* status = label("记录未启动", "badge");
    tools->addWidget(status);
    tools->addStretch();
    tools->addWidget(label("最长连续记录 2.5 小时，达到时限后自动停止并保存。", "muted"));
    auto drawing = std::make_shared<bool>(false);
    QObject::connect(draw, &QPushButton::clicked, page, [=] {
        *drawing = !*drawing;
        draw->setText(i18n::text(*drawing ? "停止绘制" : "开始绘制"));
        drawStatus->setText(i18n::text(*drawing ? "正在绘制" : "绘制已停止"));
    });
    QObject::connect(record, &QPushButton::clicked, page, [=] {
        if (c.temperatureRecorder->isActive())
            c.temperatureRecorder->stop();
        else
            c.temperatureRecorder->start(true);
    });
    QObject::connect(c.temperatureRecorder, &RecordingService::changed, page, [=](bool active, QString) {
        record->setText(i18n::text(active ? "停止温度记录" : "开始温度记录"));
        status->setText(i18n::text(active ? "记录中 · 自动停止 2.5 h" : "记录已停止"));
    });
    QObject::connect(open, &QPushButton::clicked, page, [=] {
        QDir().mkpath(c.temperatureRecorder->directory());
        QDesktopServices::openUrl(QUrl::fromLocalFile(c.temperatureRecorder->directory()));
    });
    QObject::connect(clear, &QPushButton::clicked, page, [=] {
        motor->clear();
        driver->clear();
    });
    QObject::connect(c.device, &AsyncDeviceService::telemetryReceived, page, [=](Telemetry d) {
        if (!*drawing)
            return;
        motor->append(d.seconds, d.motorTemperature);
        driver->append(d.seconds, d.driverTemperature);
    });
    return page;
}
} // 命名空间 studio::ui
