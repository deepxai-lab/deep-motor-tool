#include "DeviceManagerDialog.h"
#include "PageWidgets.h"
#include <QSpinBox>
#include <QTabWidget>

namespace studio::ui {
using namespace widgets;

QDialog* showDeviceManager(DeviceRegistry* registry, QWidget* parent) {
    auto* dialog = new QDialog(parent);
    dialog->setObjectName("deviceManagerDialog");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(i18n::text("通信板与电机管理"));
    dialog->resize(650, 520);
    auto* layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);
    layout->addWidget(label("通信与设备配置", "title"));
    layout->addWidget(
        label("请先完成电脑网卡配置，再输入通信板 IPv4 地址，并配置 CAN 通道和电机 ID。",
              "muted"));

    auto* tabs = new QTabWidget;
    tabs->setObjectName("deviceManagerTabs");
    layout->addWidget(tabs);
    auto* boardPage = new QWidget;
    auto* motorPage = new QWidget;
    boardPage->setObjectName("tabPage");
    motorPage->setObjectName("tabPage");
    tabs->addTab(boardPage, i18n::text("通信板"));
    tabs->addTab(motorPage, i18n::text("电机"));

    auto* boards = new QVBoxLayout(boardPage);
    boards->setSpacing(12);
    auto* boardList = new QComboBox;
    boards->addWidget(boardList);
    auto* ip = new QLineEdit("192.168.1.121");
    auto* rate = new QComboBox;
    rate->addItem("5 Mbps", 5000000);
    rate->addItem("8 Mbps", 8000000);
    auto* protocol = new QComboBox;
    protocol->setObjectName("boardProtocolCombo");
    protocol->addItem("CAN FD · EtherCAN v1");
    auto* form = new QFormLayout;
    form->addRow(i18n::text("IPv4 地址"), ip);
    form->addRow(i18n::text("协议"), protocol);
    form->addRow(i18n::text("数据波特率"), rate);
    boards->addLayout(form);
    boards->addWidget(
        label("仲裁波特率固定为 1 Mbps，数据波特率支持 5 Mbps 和 8 Mbps。", "muted"));
    auto* addBoard = button("添加通信板", "primary");
    auto* deleteBoard = button("删除板及关联电机", "danger");
    boards->addWidget(row({addBoard, deleteBoard}));
    boards->addStretch();

    auto* motors = new QVBoxLayout(motorPage);
    motors->setSpacing(12);
    auto* motorList = new QComboBox;
    motors->addWidget(motorList);
    auto* parentBoard = new QComboBox;
    auto* channel = new QSpinBox;
    channel->setRange(0, 3);
    auto* id = new QSpinBox;
    id->setRange(0, 15);
    id->setValue(1);
    auto* motorForm = new QFormLayout;
    motorForm->addRow(i18n::text("所属通信板"), parentBoard);
    motorForm->addRow(i18n::text("CAN 通道"), channel);
    motorForm->addRow(i18n::text("电机 ID"), id);
    motors->addLayout(motorForm);
    motors->addWidget(
        label("不限定电机型号；设备地址由通信板 IP、CAN 通道和电机 ID 唯一确定。", "muted"));
    auto* addMotor = button("添加电机", "primary");
    auto* deleteMotor = button("删除电机", "danger");
    motors->addWidget(row({addMotor, deleteMotor}));
    motors->addStretch();

    auto* status = label("修改设备配置后，当前连接将自动断开。", "muted");
    layout->addWidget(status);
    const auto refresh = [=] {
        boardList->clear();
        parentBoard->clear();
        motorList->clear();
        for (const auto& board : registry->boards()) {
            boardList->addItem(
                QString("%1 · %2 Mbps").arg(board.ip).arg(board.dataBitrate / 1000000), board.ip);
            parentBoard->addItem(board.ip);
        }
        for (const auto& motor : registry->motors())
            motorList->addItem(motor.key());
    };
    refresh();
    QObject::connect(registry, &DeviceRegistry::changed, dialog, refresh);
    QObject::connect(registry, &DeviceRegistry::errorOccurred, status,
                     [status](const QString& message) { status->setText(i18n::text(message)); });
    QObject::connect(addBoard, &QPushButton::clicked, dialog, [=] {
        registry->addBoard({ip->text(), rate->currentData().toUInt(), "deep.ethercan.canfd.v1"});
    });
    QObject::connect(deleteBoard, &QPushButton::clicked, dialog,
                     [=] { registry->removeBoard(boardList->currentData().toString()); });
    QObject::connect(addMotor, &QPushButton::clicked, dialog, [=] {
        registry->addMotor({parentBoard->currentText(), quint8(channel->value()), quint8(id->value())});
    });
    QObject::connect(deleteMotor, &QPushButton::clicked, dialog,
                     [=] { registry->removeMotor(motorList->currentIndex()); });
    dialog->setWindowModality(Qt::WindowModal);
    dialog->show();
    return dialog;
}

} // 命名空间 studio::ui
