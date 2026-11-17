#include "ui/PageWidgets.h"
#include <QStyle>

namespace studio::ui {
using namespace widgets;

QWidget* createIdentityPage(PageContext c) {
    auto* page = new QWidget;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->addStretch(1);

    QVBoxLayout* fields;
    auto* panel = card("条形码扫描", fields);
    panel->setMinimumWidth(620);
    panel->setMaximumWidth(820);
    fields->setContentsMargins(28, 24, 28, 24);
    fields->setSpacing(18);
    fields->addWidget(
        label("扫描枪按键盘方式输入。请先确认设备类型和设备 ID，再将光标留在序列号输入框。", "muted"));

    auto* form = new QFormLayout;
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    form->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    form->setHorizontalSpacing(18);
    form->setVerticalSpacing(16);
    auto* type = new QLineEdit(c.identity->type);
    auto* id = new QLineEdit(c.identity->deviceId);
    auto* serial = new QLineEdit(c.identity->serial);
    type->setObjectName("barcodeType");
    id->setObjectName("barcodeDeviceId");
    serial->setObjectName("barcodeSerial");
    for (auto* input : {type, id, serial}) {
        input->setMaxLength(128);
        input->setMinimumHeight(42);
    }
    type->setPlaceholderText(i18n::text("例如 03"));
    id->setPlaceholderText(i18n::text("输入或扫描设备 ID"));
    serial->setPlaceholderText(i18n::text("扫描序列号后按回车确认"));
    form->addRow(i18n::text("设备类型："), type);
    form->addRow(i18n::text("设备 ID："), id);
    form->addRow(i18n::text("序列号："), serial);
    fields->addLayout(form);

    auto* status = label("等待扫描", "badge");
    fields->addWidget(status);
    auto* confirm = button("确认本台设备", "primary");
    confirm->setObjectName("barcodeConfirmButton");
    auto* next = button("下一台设备");
    fields->addWidget(row({confirm, next}));

    const auto sync = [=] {
        c.identity->type = type->text().trimmed();
        c.identity->deviceId = id->text().trimmed();
        c.identity->serial = serial->text().trimmed();
    };
    const auto markInvalid = [](QLineEdit* input, bool invalid) {
        input->setProperty("validationError", invalid);
        input->style()->unpolish(input);
        input->style()->polish(input);
    };
    for (auto* input : {type, id, serial}) {
        QObject::connect(input, &QLineEdit::textChanged, page, sync);
        QObject::connect(input, &QLineEdit::textChanged, page, [=] {
            if (!input->text().trimmed().isEmpty())
                markInvalid(input, false);
        });
    }
    QObject::connect(type, &QLineEdit::returnPressed, id, qOverload<>(&QWidget::setFocus));
    QObject::connect(id, &QLineEdit::returnPressed, serial, qOverload<>(&QWidget::setFocus));
    const auto accept = [=] {
        sync();
        QList<QPair<QString, QLineEdit*>> missing;
        if (c.identity->type.isEmpty())
            missing.append({i18n::text("设备类型"), type});
        if (c.identity->deviceId.isEmpty())
            missing.append({i18n::text("设备 ID"), id});
        if (c.identity->serial.isEmpty())
            missing.append({i18n::text("序列号"), serial});
        for (auto* input : {type, id, serial})
            markInvalid(input, false);
        if (!missing.isEmpty()) {
            QStringList names;
            for (const auto& field : missing) {
                names.append(field.first);
                markInvalid(field.second, true);
            }
            status->setText(i18n::text("请填写或扫描：") + names.join(i18n::isEnglish() ? ", " : "、"));
            missing.first().second->setFocus(Qt::OtherFocusReason);
            missing.first().second->selectAll();
            return;
        }
        status->setText(i18n::text("已确认") + " · " + c.identity->serial);
        if (c.identityConfirmed)
            c.identityConfirmed();
    };
    QObject::connect(serial, &QLineEdit::returnPressed, page, accept);
    QObject::connect(confirm, &QPushButton::clicked, page, accept);
    QObject::connect(next, &QPushButton::clicked, page, [=] {
        serial->clear();
        markInvalid(serial, false);
        serial->setFocus(Qt::OtherFocusReason);
        status->setText(i18n::text("等待扫描下一台设备"));
    });
    outer->addWidget(panel, 0, Qt::AlignHCenter);
    outer->addStretch(2);
    return page;
}
} // 命名空间 studio::ui
