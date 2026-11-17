#include "ui/PageWidgets.h"
#include <QDoubleValidator>
#include <QElapsedTimer>
#include <QLocale>
#include <QTimer>

namespace studio::ui {
using namespace widgets;

QWidget* createInspectionPage(PageContext c) {
    auto* page = new QWidget;
    auto* h = new QHBoxLayout(page);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(16);
    auto* split = new QSplitter(Qt::Horizontal);
    split->setChildrenCollapsible(false);
    h->addWidget(split, 1);
    auto* leftWidget = new QWidget;
    auto* left = new QVBoxLayout(leftWidget);
    left->setContentsMargins(0, 0, 0, 0);
    left->setSpacing(12);
    QVBoxLayout* ranges;
    left->addWidget(card("01   测试阈值", ranges));
    ranges->addWidget(label("将校准、线性度与运动误差检查组合为一次可追溯测试。", "muted"));
    auto* form = new QFormLayout;
    form->setSpacing(16);
    std::array<QLineEdit*, 4> values{};
    QStringList titles{"线性误差阈值", "速度力矩误差阈值", "角度误差阈值", "位置力矩误差阈值"};
    for (int i = 0; i < 4; ++i) {
        values[i] = new QLineEdit;
        values[i]->setObjectName(QStringLiteral("inspectionThreshold%1").arg(i));
        values[i]->setPlaceholderText(QStringLiteral("0.0"));
        values[i]->setAlignment(Qt::AlignRight);
        auto* validator = new QDoubleValidator(-999999.0, 999999.0, 5, values[i]);
        validator->setLocale(QLocale::c());
        validator->setNotation(QDoubleValidator::StandardNotation);
        values[i]->setValidator(validator);
        form->addRow(i18n::text(titles[i]), values[i]);
    }
    ranges->addLayout(form);
    auto* start = button("开始检查", "primary");
    auto* stop = button("停止");
    stop->setObjectName("inspectionStop");
    ranges->addWidget(row({start, stop}));
    QVBoxLayout* process;
    left->addWidget(card("02   测试状态", process));
    auto* state = label("未知状态", "section");
    state->setObjectName("inspectionState");
    process->addWidget(state);
    QVBoxLayout* statusWordBox;
    left->addWidget(card("03   STATUSWORD", statusWordBox));
    auto* statusWord = label("未知状态", "section");
    statusWord->setObjectName("inspectionStatusWord");
    statusWordBox->addWidget(statusWord);
    left->addStretch();
    auto* leftScroll = new QScrollArea;
    leftScroll->setWidgetResizable(true);
    leftScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    leftScroll->setFrameShape(QFrame::NoFrame);
    leftScroll->setWidget(leftWidget);
    split->addWidget(leftScroll);
    QVBoxLayout* feedback;
    split->addWidget(card("04   测试反馈数据", feedback));
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 1);
    auto* elapsed = label("测试用时  00:00:00", "section");
    elapsed->setObjectName("inspectionElapsed");
    elapsed->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    feedback->addWidget(elapsed, 0, Qt::AlignRight);
    QStringList names{"线性误差",       "速度力矩最小值", "速度力矩最大值", "速度力矩波动范围",
                      "角度误差最大值", "位置力矩最小值", "位置力矩最大值", "位置力矩波动范围"};
    std::array<QLabel*, 8> labels{};
    auto* grid = new QGridLayout;
    grid->setVerticalSpacing(21);
    for (int i = 0; i < 8; ++i) {
        grid->addWidget(label(names[i], "muted"), i, 0);
        labels[i] = label("0.0", "section");
        labels[i]->setAlignment(Qt::AlignRight);
        grid->addWidget(labels[i], i, 1);
    }
    feedback->addLayout(grid);
    feedback->addStretch();
    struct Snapshot {
        Identity identity;
        DeviceAddress address;
    };
    auto snapshot = std::make_shared<Snapshot>();
    auto inspectionClock = std::make_shared<QElapsedTimer>();
    auto elapsedTimer = new QTimer(page);
    elapsedTimer->setInterval(200);
    const auto refreshElapsed = [=] {
        const qint64 seconds = inspectionClock->isValid() ? inspectionClock->elapsed() / 1000 : 0;
        elapsed->setText(i18n::text("测试用时  %1:%2:%3")
                             .arg(seconds / 3600, 2, 10, QLatin1Char('0'))
                             .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
                             .arg(seconds % 60, 2, 10, QLatin1Char('0')));
    };
    QObject::connect(elapsedTimer, &QTimer::timeout, page, refreshElapsed);
    QObject::connect(start, &QPushButton::clicked, page, [=] {
        snapshot->identity = *c.identity;
        snapshot->address = c.device->address();
        inspectionClock->start();
        elapsedTimer->start();
        refreshElapsed();
        state->setText(i18n::text("未知状态"));
        statusWord->setText(i18n::text("未知状态"));
        setStatusTone(statusWord, "");
        for (auto* valueLabel : labels)
            valueLabel->setText("0.0");
        std::array<float, 4> scope{};
        for (int i = 0; i < 4; ++i)
            scope[i] = values[i]->text().trimmed().toFloat();
        c.device->startInspection(scope);
    });
    QObject::connect(stop, &QPushButton::clicked, page, [=] {
        c.device->stopInspection();
        elapsedTimer->stop();
        state->setText(i18n::text("未知状态"));
        statusWord->setText(i18n::text("未知状态"));
        setStatusTone(statusWord, "");
    });
    QObject::connect(c.device, &AsyncDeviceService::inspectionStatusChanged, page, [=](const QString& text) {
        state->setText(i18n::text(text));
        if (text == "设置失败" || text == "测试失败")
            elapsedTimer->stop();
    });
    QObject::connect(c.device, &AsyncDeviceService::inspectionStatusWordChanged, page,
                     [=](quint16 word, const QString& text) {
                         statusWord->setText(i18n::text(text));
                         setStatusTone(statusWord, word ? "danger"
                                                       : (text == "状态正常" ? "success" : ""));
                     });
    QObject::connect(c.device, &AsyncDeviceService::inspectionStep, page, [=](int step) {
        const QStringList steps{"未知状态",     "输入轴校准",   "输出轴校准",   "线性度检测",
                                "速度力矩检测", "角度误差检测", "位置力矩检测", "检测完成"};
        state->setText(i18n::text(steps.value(step, "未知阶段")));
    });
    QObject::connect(c.device, &AsyncDeviceService::inspectionDataReceived, page, [=](const TestResult& r) {
        for (int i = 0; i < 8; ++i)
            labels[i]->setText(QString::number(r.values[i], 'f', 5));
    });
    QObject::connect(c.device, &AsyncDeviceService::inspectionFinished, page, [=](const TestResult& r) {
        elapsedTimer->stop();
        for (int i = 0; i < 8; ++i)
            labels[i]->setText(QString::number(r.values[i], 'f', 5));
        c.motionRecorder->saveTest(snapshot->identity, snapshot->address, r);
    });
    auto refresh = [=] {
        start->setEnabled(c.device->connected() && !c.device->enabled() && !c.device->busy());
        stop->setEnabled(c.device->operation() == DeviceOperation::Inspection);
        if (!c.device->connected()) {
            elapsedTimer->stop();
            state->setText(i18n::text("未知状态"));
            statusWord->setText(i18n::text("未知状态"));
            setStatusTone(statusWord, "");
        }
    };
    QObject::connect(c.device, &AsyncDeviceService::stateChanged, page, refresh);
    refresh();
    return page;
}
} // 命名空间 studio::ui
