#include "ui/PageWidgets.h"
#include <QTabWidget>
#include <array>

namespace studio::ui {
using namespace widgets;

namespace {
QWidget* parameterPage(const QList<QPair<QString, QDoubleSpinBox*>>& rows) {
    auto* page = new QWidget;
    page->setObjectName("tabPage");
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(18, 18, 18, 18);
    auto* form = new QFormLayout;
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(12);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    for (const auto& row : rows)
        form->addRow(i18n::text(row.first), row.second);
    layout->addLayout(form);
    layout->addStretch();
    return page;
}
} // 匿名命名空间

void showParameters(AsyncDeviceService* d, QWidget* parent) {
    // MainWindow 会在未连接时禁用入口；对话框入口也继续防御性校验，
    // 避免排队的点击或其他调用方在无效会话上启动参数读取。
    if (!d || !d->connected())
        return;

    auto* dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(i18n::text("电机参数"));
    dialog->setMinimumSize(540, 430);

    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 20, 24, 20);
    layout->setSpacing(12);
    layout->addWidget(label("参数配置", "title"));
    layout->addWidget(label("参数按协议功能分组。下方按钮只应用当前页签中的参数；写入前需保持电机失能。", "muted"));

    std::array<QDoubleSpinBox*, 9> fields{};
    const std::array<double, 9> maximums{99, 15, 255, 65535, 6553.5, 6553.5, 6553.5, 6553.5, 6553.5};
    for (int i = 0; i < int(fields.size()); ++i) {
        const int decimals = (i >= 1 && i <= 3) ? 0 : (i >= 4 ? 1 : 2);
        fields[i] = spin(0, maximums[i], 0, decimals);
    }
    fields[1]->setObjectName("motorIdParameter");
    fields[1]->setToolTip(i18n::text("设备应用新 ID 后，请在设备管理中选择新 ID 并重新连接。"));

    auto* tabs = new QTabWidget;
    tabs->setObjectName("parameterTabs");
    tabs->addTab(parameterPage({{"减速比", fields[0]},
                                {"电机 ID / 关节 ID", fields[1]},
                                {"电流环带宽", fields[3]},
                                {"CAN 超时", fields[2]}}),
                 i18n::text("电机参数"));
    tabs->addTab(parameterPage({{"欠压阈值 / V", fields[5]},
                                {"过压阈值 / V", fields[6]},
                                {"力矩限幅 / N·m", fields[4]},
                                {"电机过温阈值 / °C", fields[7]},
                                {"驱动器过温阈值 / °C", fields[8]}}),
                 i18n::text("保护机制"));

    auto* dataRatePage = new QWidget;
    dataRatePage->setObjectName("tabPage");
    auto* dataRateLayout = new QVBoxLayout(dataRatePage);
    dataRateLayout->setContentsMargins(18, 18, 18, 18);
    dataRateLayout->setSpacing(14);
    auto* dataRateForm = new QFormLayout;
    auto* dataRate = new QComboBox;
    dataRate->setObjectName("canFdDataRate");
    dataRate->addItem("5 Mbps", 5);
    dataRate->addItem("8 Mbps", 8);
    const int currentRate = int(d->session().dataBitrate / 1000000);
    dataRate->setCurrentIndex(qMax(0, dataRate->findData(currentRate)));
    dataRateForm->addRow(i18n::text("CAN FD数据波特率"), dataRate);
    dataRateLayout->addLayout(dataRateForm);
    auto* sendDataRate = button("发送修改", "primary");
    sendDataRate->setObjectName("sendCanFdDataRate");
    auto* dataRateStatus = label("等待发送", "badge");
    dataRateStatus->setObjectName("canFdDataRateStatus");
    dataRateLayout->addWidget(sendDataRate);
    dataRateLayout->addWidget(dataRateStatus);
    dataRateLayout->addStretch();
    tabs->addTab(dataRatePage, i18n::text("CAN FD波特率"));
    layout->addWidget(tabs, 1);

    auto* read = button("重新读取");
    auto* apply = button("仅本次应用");
    auto* save = button("写入设备", "primary");
    auto* parameterActions = row({read, apply, save});
    layout->addWidget(parameterActions);
    QObject::connect(tabs, &QTabWidget::currentChanged, dialog,
                     [=](int index) { parameterActions->setVisible(index < 2); });

    QObject::connect(d, &AsyncDeviceService::parametersReceived, dialog, [=](const QList<double>& values) {
        const auto count = qMin(qsizetype(fields.size()), values.size());
        for (qsizetype i = 0; i < count; ++i)
            fields[size_t(i)]->setValue(values[i]);
    });
    QObject::connect(read, &QPushButton::clicked, d, &AsyncDeviceService::readParameters);
    const auto write = [=](bool persist) {
        QList<double> values;
        for (auto* field : fields)
            values.append(field->value());
        const auto group = tabs->currentIndex() == 0 ? ParameterGroup::Motor : ParameterGroup::Protection;
        d->writeParameters(values, group, persist);
    };
    QObject::connect(apply, &QPushButton::clicked, dialog, [=] { write(false); });
    QObject::connect(save, &QPushButton::clicked, dialog, [=] { write(true); });
    auto* dataRateTimeout = new QTimer(dialog);
    dataRateTimeout->setSingleShot(true);
    dataRateTimeout->setInterval(1500);
    QObject::connect(sendDataRate, &QPushButton::clicked, dialog, [=] {
        sendDataRate->setEnabled(false);
        dataRateStatus->setText(i18n::text("正在发送命令37…"));
        d->setCanFdDataRate(quint8(dataRate->currentData().toUInt()));
    });
    QObject::connect(d, &AsyncDeviceService::canFdDataRateRequestSent, dialog,
                     [=](quint8 requested, bool success) {
        if (!success) {
            dataRateStatus->setText(i18n::text("命令37发送失败"));
            sendDataRate->setEnabled(d->connected() && !d->enabled() && !d->busy());
            return;
        }
        dataRateStatus->setText(
            i18n::text("命令37已发送 · DLC 1 · Byte0=%1 Mbps · 等待设备应答")
                .arg(requested));
        dataRateTimeout->start();
    });
    QObject::connect(d, &AsyncDeviceService::canFdDataRateResponse, dialog,
                     [=](quint8 requested, quint8 actual, bool success) {
        dataRateTimeout->stop();
        dataRateStatus->setText(
            success ? i18n::text("已收到设备应答 · 命令37 · DLC 1 · Byte0=%1 Mbps").arg(actual)
                    : i18n::text("已收到设备应答但数据不匹配 · 请求%1 Mbps · 应答%2 Mbps")
                          .arg(requested)
                          .arg(actual));
        sendDataRate->setEnabled(d->connected() && !d->enabled() && !d->busy());
    });
    QObject::connect(dataRateTimeout, &QTimer::timeout, dialog, [=] {
        dataRateStatus->setText(i18n::text("未收到命令37设备应答"));
        sendDataRate->setEnabled(d->connected() && !d->enabled() && !d->busy());
    });
    const auto refresh = [=] {
        const bool writable = d->connected() && !d->enabled() && !d->busy();
        read->setEnabled(d->connected() && !d->busy());
        apply->setEnabled(writable);
        save->setEnabled(writable);
        sendDataRate->setEnabled(writable && !dataRateTimeout->isActive());
    };
    QObject::connect(d, &AsyncDeviceService::stateChanged, dialog, refresh);
    refresh();

    dialog->setWindowModality(Qt::WindowModal);
    dialog->show();
    d->readParameters();
}
} // 命名空间 studio::ui
