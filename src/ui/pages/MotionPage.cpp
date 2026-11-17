#include "ui/PageWidgets.h"
#include <QApplication>
#include <QDateTime>
#include <QSettings>
#include <QTabWidget>
#include <QTimer>
#include <memory>
#include <optional>

namespace studio::ui {
using namespace widgets;

namespace {
void showMotionParametersDialog(PageContext c, QWidget* parent) {
    if (auto* existing = parent->findChild<QDialog*>("motionParametersDialog")) {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }

    auto* dialog = new QDialog(parent);
    dialog->setObjectName("motionParametersDialog");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(i18n::text("运动参数"));
    dialog->setWindowModality(Qt::NonModal);
    dialog->setMinimumWidth(430);
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(14);
    layout->addWidget(label("固定目标设置", "title"));

    auto* form = new QFormLayout;
    form->setSpacing(10);
    auto* position = spin(-100, 100, 0);
    auto* speed = spin(-100, 100, 0);
    auto* feedforward = spin(-500, 500, 0);
    auto* kp = spin(0, 1023, 10);
    auto* kd = spin(0, 51, 0.5);
    position->setObjectName("fixedPosition");
    speed->setObjectName("fixedVelocity");
    feedforward->setObjectName("fixedTorque");
    kp->setObjectName("fixedKp");
    kd->setObjectName("fixedKd");
    {
        QSettings settings;
        position->setValue(settings.value("motion/fixed/position", 0.0).toDouble());
        speed->setValue(settings.value("motion/fixed/velocity", 0.0).toDouble());
        feedforward->setValue(settings.value("motion/fixed/torque", 0.0).toDouble());
        kp->setValue(settings.value("motion/fixed/kp", 10.0).toDouble());
        kd->setValue(settings.value("motion/fixed/kd", 0.5).toDouble());
    }
    form->addRow(i18n::text("期望角度 / rad"), position);
    form->addRow(i18n::text("期望角速度 / rad/s"), speed);
    form->addRow(i18n::text("前馈力矩 / N·m"), feedforward);
    form->addRow(i18n::text("位置刚度 Kp"), kp);
    form->addRow(i18n::text("速度刚度 Kd"), kd);
    layout->addLayout(form);

    auto* state = label("已停止", "muted");
    layout->addWidget(state);
    auto* send = button("发送参数", "primary");
    auto* stop = button("停止发送");
    layout->addWidget(row({send, stop}));
    const auto saveValues = [=] {
        QSettings settings;
        settings.setValue("motion/fixed/position", position->value());
        settings.setValue("motion/fixed/velocity", speed->value());
        settings.setValue("motion/fixed/torque", feedforward->value());
        settings.setValue("motion/fixed/kp", kp->value());
        settings.setValue("motion/fixed/kd", kd->value());
        settings.sync();
    };
    QObject::connect(dialog, &QDialog::finished, dialog, [=](int) { saveValues(); });
    QObject::connect(send, &QPushButton::clicked, dialog, [=] {
        saveValues();
        MotionProgram program;
        program.target = {float(position->value()), float(speed->value()), float(feedforward->value()),
                          float(kp->value()), float(kd->value())};
        c.device->startMotion(program);
    });
    QObject::connect(stop, &QPushButton::clicked, c.device, &AsyncDeviceService::stopMotion);
    const auto refresh = [=] {
        const bool ready = c.device->connected() && c.device->enabled() && !c.device->busy();
        send->setEnabled(ready);
        stop->setEnabled(c.device->connected() && c.device->motionActive());
        state->setText(i18n::text(c.device->motionActive() ? "正在发送" : "已停止"));
        setStatusTone(state, c.device->motionActive() ? "success" : "");
    };
    QObject::connect(c.device, &AsyncDeviceService::stateChanged, dialog, refresh);
    QObject::connect(c.device, &AsyncDeviceService::motionStateChanged, dialog, [=](bool) { refresh(); });
    refresh();
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void showCurveDialog(PageContext c, QWidget* parent) {
    if (auto* existing = parent->findChild<QDialog*>("curveFollowDialog")) {
        existing->show();
        existing->raise();
        existing->activateWindow();
        return;
    }

    auto* dialog = new QDialog(parent);
    dialog->setObjectName("curveFollowDialog");
    // 对话框隐藏时仍保留对话框和编辑器对象，使再次打开时值不变，
    // 同时避免在 500 Hz 运动流运行期间执行阻塞式 QSettings 同步。
    dialog->setAttribute(Qt::WA_DeleteOnClose, false);
    dialog->setWindowTitle(i18n::text("曲线跟随"));
    dialog->setWindowModality(Qt::NonModal);
    dialog->setMinimumWidth(440);
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(14);
    layout->addWidget(label("周期运动设置", "title"));
    layout->addWidget(
        label("独立配置位置或力矩正弦指令。启动后可关闭窗口，主界面继续显示目标与反馈。", "muted"));

    auto* tabs = new QTabWidget;
    tabs->setObjectName("curveFollowTabs");

    auto* torquePage = new QWidget;
    auto* torqueForm = new QFormLayout(torquePage);
    torqueForm->setContentsMargins(16, 18, 16, 18);
    torqueForm->setSpacing(12);
    auto* torqueAmplitude = spin(0, 100, 0);
    auto* torqueFrequency = spin(0, 20, 0);
    torqueAmplitude->setObjectName("torqueAmplitude");
    torqueFrequency->setObjectName("torqueFrequency");
    {
        QSettings settings;
        torqueAmplitude->setValue(settings.value("motion/curve/torqueAmplitude", 0.0).toDouble());
        torqueFrequency->setValue(settings.value("motion/curve/torqueFrequency", 0.0).toDouble());
    }
    torqueForm->addRow(i18n::text("幅值"), torqueAmplitude);
    torqueForm->addRow(i18n::text("频率 / Hz"), torqueFrequency);
    tabs->addTab(torquePage, i18n::text("力矩跟随"));

    auto* positionPage = new QWidget;
    auto* positionForm = new QFormLayout(positionPage);
    positionForm->setContentsMargins(16, 18, 16, 18);
    positionForm->setSpacing(12);
    auto* positionAmplitude = spin(0, 100, 0);
    auto* positionFrequency = spin(0, 20, 0);
    auto* offset = spin(-100, 100, 0);
    auto* kp = spin(0, 1023, 0);
    auto* kd = spin(0, 51, 0);
    positionAmplitude->setObjectName("positionAmplitude");
    positionFrequency->setObjectName("positionFrequency");
    offset->setObjectName("positionOffset");
    kp->setObjectName("positionKp");
    kd->setObjectName("positionKd");
    {
        QSettings settings;
        positionAmplitude->setValue(settings.value("motion/curve/positionAmplitude", 0.0).toDouble());
        positionFrequency->setValue(settings.value("motion/curve/positionFrequency", 0.0).toDouble());
        offset->setValue(settings.value("motion/curve/positionOffset", 0.0).toDouble());
        kp->setValue(settings.value("motion/curve/positionKp", 0.0).toDouble());
        kd->setValue(settings.value("motion/curve/positionKd", 0.0).toDouble());
    }
    positionForm->addRow(i18n::text("幅值"), positionAmplitude);
    positionForm->addRow(i18n::text("频率 / Hz"), positionFrequency);
    positionForm->addRow(i18n::text("位置偏置 / rad"), offset);
    positionForm->addRow(i18n::text("位置刚度 Kp"), kp);
    positionForm->addRow(i18n::text("速度刚度 Kd"), kd);
    tabs->addTab(positionPage, i18n::text("位置跟随"));
    tabs->setCurrentIndex(qBound(0, QSettings().value("motion/curve/tab", 1).toInt(), 1));
    layout->addWidget(tabs);

    auto* state = label("已停止", "muted");
    state->setObjectName("curveFollowState");
    layout->addWidget(state);
    auto* start = button("开始跟随", "primary");
    auto* stop = button("停止发送");
    stop->setObjectName("curveFollowStop");
    layout->addWidget(row({start, stop}));
    const auto saveParameters = [=] {
        QSettings settings;
        settings.setValue("motion/curve/tab", tabs->currentIndex());
        settings.setValue("motion/curve/torqueAmplitude", torqueAmplitude->value());
        settings.setValue("motion/curve/torqueFrequency", torqueFrequency->value());
        settings.setValue("motion/curve/positionAmplitude", positionAmplitude->value());
        settings.setValue("motion/curve/positionFrequency", positionFrequency->value());
        settings.setValue("motion/curve/positionOffset", offset->value());
        settings.setValue("motion/curve/positionKp", kp->value());
        settings.setValue("motion/curve/positionKd", kd->value());
        settings.sync();
    };
    QObject::connect(dialog, &QDialog::finished, dialog, [=](int) {
        if (!c.device->motionActive())
            saveParameters();
    });
    QObject::connect(start, &QPushButton::clicked, dialog, [=] {
        MotionProgram program;
        if (tabs->currentWidget() == torquePage) {
            program.waveform = Waveform::TorqueSine;
            program.amplitude = torqueAmplitude->value();
            program.frequency = torqueFrequency->value();
        } else {
            program.waveform = Waveform::PositionSine;
            program.amplitude = positionAmplitude->value();
            program.frequency = positionFrequency->value();
            program.offset = offset->value();
            program.target.kp = float(kp->value());
            program.target.kd = float(kd->value());
        }
        c.device->startMotion(program);
    });
    QObject::connect(stop, &QPushButton::clicked, dialog, [=] {
        c.device->stopMotion();
        saveParameters();
    });
    QObject::connect(qApp, &QCoreApplication::aboutToQuit, dialog, saveParameters);
    const auto refresh = [=] {
        const bool ready = c.device->connected() && c.device->enabled() && !c.device->busy();
        start->setEnabled(ready);
        stop->setEnabled(c.device->connected() && c.device->motionActive());
        state->setText(i18n::text(c.device->motionActive() ? "正在跟随" : "已停止"));
        setStatusTone(state, c.device->motionActive() ? "success" : "");
    };
    QObject::connect(c.device, &AsyncDeviceService::stateChanged, dialog, refresh);
    QObject::connect(c.device, &AsyncDeviceService::motionStateChanged, dialog, [=](bool) { refresh(); });
    refresh();
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}
} // 匿名命名空间

QWidget* createMotionPage(PageContext c) {
    auto* page = new QWidget;
    auto* layout = new QHBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(12);

    auto* plots = new QSplitter(Qt::Vertical);
    plots->setChildrenCollapsible(false);
    auto* angle = new PlotWidget("角度", "角度 / rad");
    auto* velocity = new PlotWidget("角速度", "角速度 / rad/s");
    auto* torque = new PlotWidget("扭矩", "扭矩 / N·m");
    for (auto* plot : {angle, velocity, torque}) {
        plot->setRollingWindow(30.0, 3.0, 1.0);
        plots->addWidget(plot);
        plots->setStretchFactor(plots->count() - 1, 1);
    }
    layout->addWidget(plots, 1);

    auto* controls = new QWidget;
    auto* right = new QVBoxLayout(controls);
    right->setContentsMargins(0, 0, 0, 0);
    right->setSpacing(12);
    right->addWidget(liveStatus(c.device, page));

    QVBoxLayout* actions;
    right->addWidget(card("运行控制", actions));
    actions->addWidget(motorEnable(c.device, page));
    auto* fixed = button("运动参数");
    fixed->setObjectName("motionParametersButton");
    auto* follow = button("曲线跟随");
    follow->setObjectName("curveFollowButton");
    auto* record = button("开始记录 CSV");
    auto* openRecords = button("打开记录目录");
    auto* clear = button("清空波形");
    actions->addWidget(row({fixed, follow}));
    actions->addWidget(row({record, openRecords}));
    actions->addWidget(clear);

    QVBoxLayout* phaseBox;
    auto* phaseCard = card("UVW相位与电角度", phaseBox);
    phaseCard->setObjectName("phaseAngleCard");
    right->addWidget(phaseCard);
    auto* phaseGrid = new QGridLayout;
    phaseGrid->setHorizontalSpacing(12);
    phaseGrid->setVerticalSpacing(10);
    auto* phaseOrder = label("—", "metric");
    phaseOrder->setObjectName("phaseOrderValue");
    phaseOrder->setProperty("dataValue", true);
    auto* electricalOffset = label("—", "metric");
    electricalOffset->setObjectName("electricalAngleOffsetValue");
    electricalOffset->setProperty("dataValue", true);
    phaseGrid->addWidget(label("相序", "muted"), 0, 0);
    phaseGrid->addWidget(phaseOrder, 0, 1);
    phaseGrid->addWidget(label("电气角度偏置 / rad", "muted"), 1, 0);
    phaseGrid->addWidget(electricalOffset, 1, 1);
    phaseBox->addLayout(phaseGrid);
    auto* startPhase = button("开始发送", "primary");
    startPhase->setObjectName("startPhaseAngleQuery");
    auto* stopPhase = button("停止发送");
    stopPhase->setObjectName("stopPhaseAngleQuery");
    phaseBox->addWidget(row({startPhase, stopPhase}));
    auto* phaseStatus = label("未开始", "badge");
    phaseStatus->setObjectName("phaseAngleStatus");
    phaseBox->addWidget(phaseStatus);

    auto latestPhase = std::make_shared<std::optional<PhaseAngleData>>();
    auto* phaseDisplayTimer = new QTimer(page);
    phaseDisplayTimer->setSingleShot(true);
    phaseDisplayTimer->setInterval(20);
    QObject::connect(phaseDisplayTimer, &QTimer::timeout, page, [=] {
        if (!*latestPhase)
            return;
        const auto data = **latestPhase;
        phaseOrder->setText(data.phaseOrder == 0
                                ? i18n::text("UVW正向")
                                : data.phaseOrder == 1
                                      ? i18n::text("UWV反向")
                                      : i18n::text("未知相序（%1）").arg(data.phaseOrder));
        electricalOffset->setText(QString::number(data.electricalAngleOffset, 'f', 6));
    });
    auto acceptPhaseReplies = std::make_shared<bool>(false);
    auto phaseWaiting = std::make_shared<bool>(false);
    auto* phaseReplyTimeout = new QTimer(page);
    phaseReplyTimeout->setSingleShot(true);
    phaseReplyTimeout->setInterval(1500);
    QObject::connect(startPhase, &QPushButton::clicked, page, [=] {
        *acceptPhaseReplies = true;
        *phaseWaiting = true;
        phaseStatus->setText(i18n::text("命令已发送，等待设备应答…"));
        phaseReplyTimeout->start();
        c.device->startPhaseAngleQuery();
    });
    QObject::connect(stopPhase, &QPushButton::clicked, page, [=] {
        *acceptPhaseReplies = false;
        *phaseWaiting = false;
        phaseReplyTimeout->stop();
        phaseStatus->setText(i18n::text("已停止"));
        c.device->stopPhaseAngleQuery();
    });
    QObject::connect(c.device, &AsyncDeviceService::phaseAngleReceived, page,
                     [=](const PhaseAngleData& data) {
        if (!*acceptPhaseReplies)
            return;
        *latestPhase = data;
        if (!phaseDisplayTimer->isActive())
            phaseDisplayTimer->start();
        if (*phaseWaiting) {
            *phaseWaiting = false;
            phaseReplyTimeout->stop();
            phaseStatus->setText(i18n::text("已收到设备应答"));
        }
    });
    QObject::connect(phaseReplyTimeout, &QTimer::timeout, page, [=] {
        if (*phaseWaiting)
            phaseStatus->setText(i18n::text("未收到设备应答（仍在读取）"));
    });
    const auto refreshPhase = [=](bool) {
        const bool active = c.device->phaseAngleQueryActive();
        startPhase->setEnabled(c.device->connected() && !c.device->busy() && !active);
        stopPhase->setEnabled(active);
        if (!active) {
            *acceptPhaseReplies = false;
            *phaseWaiting = false;
            phaseReplyTimeout->stop();
            phaseStatus->setText(i18n::text("已停止"));
        }
    };
    QObject::connect(c.device, &AsyncDeviceService::phaseAngleQueryStateChanged, page, refreshPhase);
    QObject::connect(c.device, &AsyncDeviceService::stateChanged, page, [=] { refreshPhase(false); });
    refreshPhase(false);

    QVBoxLayout* counterBox;
    auto* counterCard = card("CAN FD 通讯计数", counterBox);
    counterCard->setObjectName("canFdCounterCard");
    right->addWidget(counterCard);
    auto* counterGrid = new QGridLayout;
    counterGrid->setHorizontalSpacing(12);
    counterGrid->setVerticalSpacing(8);
    auto* receiveCount = label("—", "metric");
    receiveCount->setObjectName("canFdReceiveCount");
    receiveCount->setProperty("dataValue", true);
    auto* transmitCount = label("—", "metric");
    transmitCount->setObjectName("canFdTransmitCount");
    transmitCount->setProperty("dataValue", true);
    counterGrid->addWidget(label("接收帧数 RX", "muted"), 0, 0);
    counterGrid->addWidget(receiveCount, 0, 1);
    counterGrid->addWidget(label("发送帧数 TX", "muted"), 1, 0);
    counterGrid->addWidget(transmitCount, 1, 1);
    counterBox->addLayout(counterGrid);
    auto* queryCounters = button("获取一次", "primary");
    queryCounters->setObjectName("queryCanFdCounters");
    auto* counterStatus = label("尚未获取", "badge");
    counterStatus->setObjectName("canFdCounterStatus");
    counterBox->addWidget(queryCounters);
    counterBox->addWidget(counterStatus);
    auto* counterTimeout = new QTimer(page);
    counterTimeout->setSingleShot(true);
    counterTimeout->setInterval(1500);
    QObject::connect(queryCounters, &QPushButton::clicked, page, [=] {
        queryCounters->setEnabled(false);
        counterStatus->setText(i18n::text("命令已发送，等待设备应答…"));
        counterTimeout->start();
        c.device->queryCanFdCounters();
    });
    QObject::connect(c.device, &AsyncDeviceService::canFdCountersReceived, page,
                     [=](const CanFdCounters& counters) {
        counterTimeout->stop();
        receiveCount->setText(QString::number(counters.received));
        transmitCount->setText(QString::number(counters.transmitted));
        counterStatus->setText(i18n::text("已收到设备应答 · %1")
                                   .arg(QDateTime::currentDateTime().toString("HH:mm:ss")));
        queryCounters->setEnabled(c.device->connected() && !c.device->busy());
    });
    QObject::connect(counterTimeout, &QTimer::timeout, page, [=] {
        counterStatus->setText(i18n::text("未收到设备应答"));
        queryCounters->setEnabled(c.device->connected() && !c.device->busy());
    });
    QObject::connect(c.device, &AsyncDeviceService::stateChanged, page, [=] {
        const bool connected = c.device->connected();
        queryCounters->setEnabled(connected && !c.device->busy() && !counterTimeout->isActive());
        if (!connected) {
            counterTimeout->stop();
            receiveCount->setText("—");
            transmitCount->setText("—");
            counterStatus->setText(i18n::text("等待连接设备"));
        }
    });
    queryCounters->setEnabled(c.device->connected() && !c.device->busy());

    enableButton(fixed, c.device, page, true);
    enableButton(follow, c.device, page, true);
    QObject::connect(fixed, &QPushButton::clicked, page, [=] { showMotionParametersDialog(c, page); });
    QObject::connect(follow, &QPushButton::clicked, page, [=] { showCurveDialog(c, page); });
    QObject::connect(record, &QPushButton::clicked, page, [=] {
        if (c.motionRecorder->isActive())
            c.motionRecorder->stop();
        else
            c.motionRecorder->start(false);
    });
    QObject::connect(c.motionRecorder, &RecordingService::changed, page, [=](bool active, const QString&) {
        record->setText(i18n::text(active ? "停止记录" : "开始记录 CSV"));
    });
    QObject::connect(openRecords, &QPushButton::clicked, page, [=] {
        QDir().mkpath(c.motionRecorder->directory());
        QDesktopServices::openUrl(QUrl::fromLocalFile(c.motionRecorder->directory()));
    });
    QObject::connect(clear, &QPushButton::clicked, page, [=] {
        angle->clear();
        velocity->clear();
        torque->clear();
    });
    right->addStretch();
    layout->addWidget(scrollPanel(controls));

    QObject::connect(c.device, &AsyncDeviceService::motionSampleReceived, page,
                     [=](Telemetry feedback, double sampleSeconds, Motion target) {
        if (!c.device->motionActive())
            return;
        angle->appendActual(sampleSeconds, feedback.position);
        angle->appendTarget(sampleSeconds, target.position);
        velocity->appendActual(sampleSeconds, feedback.velocity);
        velocity->appendTarget(sampleSeconds, target.velocity);
        torque->appendActual(sampleSeconds, feedback.torque);
        torque->appendTarget(sampleSeconds, target.torque);
    });
    QObject::connect(c.device, &AsyncDeviceService::motionStateChanged, page, [=](bool active) {
        if (!active) {
            angle->breakSeries();
            velocity->breakSeries();
            torque->breakSeries();
            return;
        }
        // 每次开始或重新开始时，所有图表都恢复到实时窗口。
        // 之前的手动平移、缩放不能使重新开始后的波形留在屏幕外。
        angle->resetView();
        velocity->resetView();
        torque->resetView();
    });
    return page;
}
} // 命名空间 studio::ui
