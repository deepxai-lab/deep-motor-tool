#include "ReleaseNotesDialog.h"
#include "Localization.h"
#include <QCoreApplication>
#include <QDialog>
#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QVBoxLayout>

namespace studio::ui {
namespace {
QLabel* label(const QString& value, const char* name) {
    auto* result = new QLabel(value);
    result->setObjectName(name);
    result->setTextFormat(Qt::PlainText);
    result->setWordWrap(true);
    result->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return result;
}
} // 匿名命名空间

QDialog* createReleaseNotesDialog(QWidget* parent) {
    auto* dialog = new QDialog(parent);
    dialog->setObjectName("releaseNotesDialog");
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(i18n::text("版本记录"));
    dialog->resize(860, 710);
    dialog->setMinimumSize(600, 460);
    auto* outer = new QVBoxLayout(dialog);
    outer->setContentsMargins(24, 22, 24, 18);
    outer->setSpacing(16);
    outer->addWidget(label(i18n::text("版本记录"), "releaseTitle"));
    outer->addWidget(label(i18n::text("查看当前运行版本、更新日期与功能完善内容。"),
                           "releaseSubtitle"));

    auto* scroll = new QScrollArea(dialog);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto* content = new QWidget;
    auto* sections = new QVBoxLayout(content);
    sections->setContentsMargins(0, 0, 8, 0);
    sections->setSpacing(18);
    const QString currentVersion = "V" + QCoreApplication::applicationVersion();
    auto* current = new QFrame;
    current->setObjectName("releaseCurrent");
    auto* currentLayout = new QVBoxLayout(current);
    currentLayout->setContentsMargins(24, 20, 24, 20);
    currentLayout->setSpacing(10);
    currentLayout->addWidget(label(i18n::text("当前运行版本"), "releaseSubtitle"));
    currentLayout->addWidget(label(currentVersion, "releaseVersion"));
    currentLayout->addWidget(label(i18n::text("发布前完善"), "releaseSubtitle"));
    sections->addWidget(current);

    // 更新内容只来自随安装包提供的 CHANGELOG，界面不另维护一份历史。
    QFile file(":/CHANGELOG.md");
    QVBoxLayout* details = nullptr;
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        const auto lines = QString::fromUtf8(file.readAll()).split('\n');
        for (const QString& source : lines) {
            const QString line = source.trimmed();
            if (line.startsWith("## ")) {
                const auto heading = line.mid(3).split(QChar(0x00b7));
                const QString version = heading.value(0).trimmed();
                auto* card = new QFrame;
                card->setObjectName("releaseCard");
                details = new QVBoxLayout(card);
                details->setContentsMargins(24, 20, 24, 22);
                details->setSpacing(10);
                auto* row = new QHBoxLayout;
                row->setSpacing(12);
                row->addWidget(label(version, "releaseCardVersion"));
                row->addWidget(label(heading.value(1).trimmed(), "releaseSubtitle"));
                if (version.compare(currentVersion, Qt::CaseInsensitive) == 0)
                    row->addWidget(label(i18n::text("当前版本"), "releaseBadge"));
                row->addStretch();
                details->addLayout(row);
                sections->addWidget(card);
            } else if (details && line.startsWith("### ")) {
                details->addSpacing(6);
                details->addWidget(label(line.mid(4), "releaseGroupTitle"));
            } else if (details && line.startsWith("- ")) {
                auto* row = new QHBoxLayout;
                row->setSpacing(8);
                auto* bullet = label(QStringLiteral("•"), "releaseBullet");
                bullet->setFixedWidth(12);
                row->addWidget(bullet, 0, Qt::AlignTop);
                row->addWidget(label(line.mid(2), "releaseItem"), 1);
                details->addLayout(row);
            }
        }
    }
    if (!details)
        sections->addWidget(label(i18n::text("无法读取版本更新记录"), "releaseItem"));
    sections->addStretch();
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);
    auto* buttons = new QHBoxLayout;
    buttons->addStretch();
    auto* close = new QPushButton(i18n::text("关闭"));
    close->setMinimumWidth(110);
    buttons->addWidget(close);
    outer->addLayout(buttons);
    QObject::connect(close, &QPushButton::clicked, dialog, &QDialog::close);
    return dialog;
}
} // 命名空间 studio::ui
