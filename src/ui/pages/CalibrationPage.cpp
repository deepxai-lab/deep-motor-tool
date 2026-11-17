#include "ui/PageWidgets.h"
#include <QStringList>

namespace studio::ui {
using namespace widgets;

QWidget* createCalibrationPage(PageContext c) {
    auto* page = new QWidget;
    auto* h = new QHBoxLayout(page);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(16);

    auto* graphs = new QSplitter(Qt::Vertical);
    graphs->setChildrenCollapsible(false);
    auto* angle = new PlotWidget("电机与编码器校准", "校准值");
    auto* compensation = new PlotWidget("电机齿槽转矩补偿校准", "补偿值");
    compensation->setScatterOnly(true);
    for (auto* plot : {angle, compensation}) {
        plot->setWindow(128);
        plot->setAxisLabel("采样序号");
        graphs->addWidget(plot);
        graphs->setStretchFactor(graphs->count() - 1, 1);
    }
    h->addWidget(graphs, 1);

    QVBoxLayout* tools;
    auto* pane = card("校准工作流", tools);
    h->addWidget(scrollPanel(pane, 370));
    tools->addWidget(label("失能状态下进行校准。输入轴报告将按阶段显示在左侧曲线。", "muted"));

    auto* input = button("输入轴校准", "primary");
    auto* output = button("输出轴校准");
    auto* clear = button("清空校准数据");
    input->setProperty("calibrationAction", true);
    output->setProperty("calibrationAction", true);
    clear->setProperty("calibrationAction", true);
    tools->addWidget(input);
    tools->addWidget(output);
    tools->addWidget(clear);

    auto* status = label("准备就绪", "calibrationStatus");
    status->setWordWrap(true);
    tools->addWidget(status);

    auto* resultPanel = new QFrame;
    resultPanel->setObjectName("calibrationResultPanel");
    auto* resultLayout = new QVBoxLayout(resultPanel);
    resultLayout->setContentsMargins(14, 13, 14, 13);
    resultLayout->setSpacing(11);
    auto* resultHeader = new QHBoxLayout;
    resultHeader->setSpacing(8);
    resultHeader->addWidget(label("校准状态", "calibrationPanelTitle"));
    resultHeader->addStretch();
    auto* target = label("尚未开始", "calibrationTarget");
    resultHeader->addWidget(target);
    resultLayout->addLayout(resultHeader);

    auto* completion = label("命令 31 · STEP：—", "calibrationCompletion");
    completion->setProperty("received", false);
    resultLayout->addWidget(completion);

    auto* history = label("请选择输入轴或输出轴校准", "calibrationTimeline");
    history->setMinimumHeight(92);
    history->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    history->setTextInteractionFlags(Qt::TextSelectableByMouse);
    resultLayout->addWidget(history);

    auto* outputResult = new QFrame;
    outputResult->setObjectName("calibrationOutputResult");
    auto* outputLayout = new QVBoxLayout(outputResult);
    outputLayout->setContentsMargins(10, 9, 10, 9);
    outputLayout->setSpacing(7);
    outputLayout->addWidget(label("输出轴设备反馈", "calibrationOutputTitle"));
    auto* outputAck = label("等待设备应答", "calibrationOutputAck");
    outputLayout->addWidget(outputAck);
    auto* outputGrid = new QGridLayout;
    outputGrid->setHorizontalSpacing(8);
    outputGrid->setVerticalSpacing(6);
    auto* stepValue = label("—", "calibrationProtocolValue");
    auto* data0Value = label("—", "calibrationProtocolValue");
    auto* data1Value = label("—", "calibrationProtocolValue");
    auto* data2Value = label("—", "calibrationProtocolValue");
    const QList<QPair<QString, QLabel*>> fields{{"STEP", stepValue}, {"DATA0", data0Value},
                                                {"DATA1", data1Value}, {"DATA2", data2Value}};
    for (int index = 0; index < fields.size(); ++index) {
        outputGrid->addWidget(label(fields[index].first, "calibrationProtocolKey"), index / 2,
                              (index % 2) * 2);
        outputGrid->addWidget(fields[index].second, index / 2, (index % 2) * 2 + 1);
    }
    outputGrid->setColumnStretch(1, 1);
    outputGrid->setColumnStretch(3, 1);
    outputLayout->addLayout(outputGrid);
    outputResult->setVisible(false);
    resultLayout->addWidget(outputResult);
    tools->addWidget(resultPanel);
    tools->addStretch();

    auto counts = std::make_shared<std::array<int, 2>>();
    auto events = std::make_shared<QStringList>();
    // 0 表示无校准，1 表示输入轴（命令 3），2 表示输出轴（命令 25）。
    auto activeCalibration = std::make_shared<int>(0);

    const auto showStatus = [=](const QString& source, const char* state) {
        status->setText(i18n::text(source));
        status->setProperty("calibrationState", state);
        status->style()->unpolish(status);
        status->style()->polish(status);
    };
    const auto showCompletion = [=](const QString& source, bool received) {
        completion->setText(i18n::text(source));
        completion->setProperty("received", received);
        completion->style()->unpolish(completion);
        completion->style()->polish(completion);
    };
    const auto addEvent = [=](const QString& source) {
        const QString translated = i18n::text(source);
        if (!events->isEmpty() && events->last() == translated)
            return;
        events->append(translated);
        while (events->size() > 6)
            events->removeFirst();
        QStringList lines;
        for (const auto& event : *events)
            lines.append(QStringLiteral("•  ") + event);
        history->setText(lines.join('\n'));
    };
    const auto clearFeedback = [=] {
        for (auto* value : {stepValue, data0Value, data1Value, data2Value})
            value->setText(QStringLiteral("—"));
        outputAck->setText(i18n::text("等待设备应答"));
        outputResult->setVisible(false);
        target->setText(i18n::text("尚未开始"));
        showCompletion("命令 31 · STEP：—", false);
        events->clear();
        history->setText(i18n::text("请选择输入轴或输出轴校准"));
    };
    const auto calibrationCanStart = [=] {
        QString reason;
        if (!c.device->connected())
            reason = QStringLiteral("请先连接设备");
        else if (c.device->enabled())
            reason = QStringLiteral("校准前请先失能");
        else if (c.device->busy() &&
                 c.device->operation() != DeviceOperation::Calibration)
            reason = QStringLiteral("当前任务进行中，请先停止或等待结束");
        if (reason.isEmpty())
            return true;
        showStatus(reason, "waiting");
        return false;
    };

    QObject::connect(input, &QPushButton::clicked, page, [=] {
        if (!calibrationCanStart())
            return;
        angle->clear();
        compensation->clear();
        *counts = {0, 0};
        clearFeedback();
        *activeCalibration = 1;
        target->setText(i18n::text("输入轴校准"));
        showCompletion("命令 31 · STEP=5：尚未收到", false);
        history->setText(i18n::text("等待设备开始校准"));
        showStatus("等待输入轴校准应答", "waiting");
        c.device->calibrate(false);
    });
    QObject::connect(output, &QPushButton::clicked, page, [=] {
        if (!calibrationCanStart())
            return;
        clearFeedback();
        *activeCalibration = 2;
        target->setText(i18n::text("输出轴校准"));
        showCompletion("命令 31 · STEP=6：尚未收到", false);
        outputResult->setVisible(true);
        history->setText(i18n::text("等待输出轴校准结果"));
        showStatus("等待输出轴校准应答", "waiting");
        c.device->calibrate(true);
    });
    QObject::connect(clear, &QPushButton::clicked, page, [=] {
        angle->clear();
        compensation->clear();
        *counts = {0, 0};
        if (!c.device->busy())
            clearFeedback();
    });

    QObject::connect(c.device, &AsyncDeviceService::inputCalibrationPoint, page,
                     [=](int series, double value) {
        if (*activeCalibration != 1)
            return;
        if (series < 0 || series >= int(counts->size()))
            return;
        auto& count = (*counts)[series];
        ++count;
        const QString latest =
            i18n::text(series == 0 ? "命令 31 · STEP=2 · 已收到 %1 帧"
                                   : "命令 31 · STEP=3 · 已收到 %1 帧")
                .arg(count);
        if (count == 1) {
            addEvent(series == 0 ? "命令 31 · STEP=2"
                                 : "命令 31 · STEP=3");
            showStatus(latest, "running");
        } else {
            status->setText(latest);
        }
        (series == 0 ? angle : compensation)->append(count - 1, value);
    });
    QObject::connect(c.device, &AsyncDeviceService::inputCalibrationCommandResponse, page,
                     [=](quint16 result) {
        if (*activeCalibration != 1)
            return;
        if (result != 0) {
            addEvent(i18n::text("输入轴校准失败 · 错误码 %1").arg(result));
            showStatus(QStringLiteral("输入轴校准失败 · 错误码 %1").arg(result), "error");
            *activeCalibration = 0;
        }
    });
    QObject::connect(c.device, &AsyncDeviceService::outputCalibrationCommandResponse, page,
                     [=](quint16 result) {
        if (*activeCalibration != 2)
            return;
        outputAck->setText(i18n::text("设备应答 · 返回码 %1").arg(result));
        if (result == 0) {
            history->setText(i18n::text("输出端编码器校准中"));
            showStatus("输出端编码器校准中", "running");
        } else {
            addEvent(i18n::text("输出轴校准失败 · 错误码 %1").arg(result));
            showStatus(QStringLiteral("输出轴校准失败 · 错误码 %1").arg(result), "error");
            *activeCalibration = 0;
        }
    });
    QObject::connect(c.device, &AsyncDeviceService::inputCalibrationReportReceived, page,
                     [=](quint16 step, quint16, quint16 data1, quint16) {
        if (*activeCalibration != 1)
            return;
        switch (step) {
        case 0:
            addEvent("命令 31 · STEP=0 · 校准开始");
            showStatus("命令 31 · STEP=0 · 校准开始", "running");
            break;
        case 1: {
            const QString direction =
                data1 == 0   ? i18n::text("编码器方向：反向")
                : data1 == 1 ? i18n::text("编码器方向：正向")
                             : i18n::text("未知相序");
            const QString phase = QStringLiteral("命令 31 · STEP=1 · %1").arg(direction);
            addEvent(phase);
            showStatus(phase, "running");
            break;
        }
        case 5:
            addEvent("命令 31 · STEP=5 · 校准成功");
            showStatus("命令 31 · STEP=5 · 校准成功", "success");
            showCompletion("命令 31 · STEP=5：已收到", true);
            *activeCalibration = 0;
            break;
        default:
            break;
        }
    });
    QObject::connect(c.device, &AsyncDeviceService::outputCalibrationResultReceived, page,
                     [=](quint16 data0, quint16 data1, quint16 data2) {
        if (*activeCalibration != 2)
            return;
        stepValue->setText(QStringLiteral("6"));
        data0Value->setText(QString::number(data0));
        data1Value->setText(QString::number(data1));
        data2Value->setText(QString::number(data2));
        const bool success = data0 == 0 && data1 == 0 && data2 == 0;
        const QString result =
            success ? QStringLiteral("输出轴校准成功")
                    : QStringLiteral("输出轴校准未成功 · DATA0=%1 · DATA1=%2 · DATA2=%3")
                          .arg(data0)
                          .arg(data1)
                          .arg(data2);
        addEvent(result);
        showStatus(result, success ? "success" : "error");
        showCompletion("命令 31 · STEP=6：已收到", true);
        *activeCalibration = 0;
    });
    const auto refresh = [=] {
        // 与老上位机一致：即使缺少 STEP=5/6 完成报告，
        // 也不能让任一校准控件一直处于灰色禁用状态。
        input->setEnabled(true);
        output->setEnabled(true);
        if (!c.device->connected()) {
            *activeCalibration = 0;
            showStatus("未连接", "disconnected");
        } else if (*activeCalibration != 0 &&
                   c.device->operation() != DeviceOperation::Calibration) {
            *activeCalibration = 0;
            showStatus(c.device->enabled() ? "请先失能" : "准备就绪", "idle");
        } else if (status->property("calibrationState").toString() == "disconnected" ||
                   status->property("calibrationState").toString() == "idle") {
            showStatus(c.device->enabled() ? "请先失能" : "准备就绪", "idle");
        }
    };
    QObject::connect(c.device, &AsyncDeviceService::stateChanged, page, refresh);
    refresh();
    return page;
}
} // 命名空间 studio::ui
