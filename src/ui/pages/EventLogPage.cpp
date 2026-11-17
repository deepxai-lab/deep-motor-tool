#include "ui/PageWidgets.h"
#include "ui/ThemeColors.h"
#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFont>
#include <QEvent>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QStringConverter>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextStream>

namespace studio::ui {
using namespace widgets;

namespace {
class EventLogHighlighter final : public QSyntaxHighlighter {
  public:
    explicit EventLogHighlighter(QPlainTextEdit* view) : QSyntaxHighlighter(view->document()) {
        view->installEventFilter(this);
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::PaletteChange) {
            colors_ = ThemeColors();
            rehighlight();
        }
        return QSyntaxHighlighter::eventFilter(watched, event);
    }

  protected:
    void highlightBlock(const QString& line) override {
        QTextCharFormat messageFormat;
        messageFormat.setForeground(colors_.text);
        setFormat(0, line.size(), messageFormat);

        static const QRegularExpression timestampPattern(
            QStringLiteral(R"(^\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}\.\d{3})"));
        const auto timestamp = timestampPattern.match(line);
        const int messageStart = timestamp.hasMatch() ? timestamp.capturedEnd() : 0;
        if (timestamp.hasMatch()) {
            QTextCharFormat timestampFormat;
            timestampFormat.setForeground(colors_.muted);
            setFormat(timestamp.capturedStart(), timestamp.capturedLength(), timestampFormat);
        }

        const auto message = line.mid(messageStart);
        QColor accent;
        if (containsAny(message, {"失败", "异常", "错误", "中断", "超时", "断开", "failed", "error",
                                  "timeout", "disconnected"}))
            accent = colors_.danger;
        else if (containsAny(message, {"停止", "取消", "失能", "警告", "stopped", "cancelled", "warning"}))
            accent = colors_.warning;
        else if (containsAny(message, {"成功", "收到", "完成", "启动", "已连接", "更新", "success",
                                       "received", "completed", "started", "connected"}))
            accent = colors_.accent;
        if (accent.isValid()) {
            QTextCharFormat accentFormat;
            accentFormat.setForeground(accent);
            setFormat(messageStart, line.size() - messageStart, accentFormat);
        }
    }

  private:
    ThemeColors colors_;
    static bool containsAny(const QString& text, std::initializer_list<const char*> words) {
        for (const auto* word : words)
            if (text.contains(QString::fromUtf8(word), Qt::CaseInsensitive))
                return true;
        return false;
    }
};
} // 匿名命名空间

QWidget* createEventLogPage(QPlainTextEdit* logView, RecordingService* recorder) {
    auto* page = new QWidget;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);

    QVBoxLayout* content;
    auto* panel = card("事件日志", content);
    auto* top = new QHBoxLayout;
    top->addWidget(label("记录连接、任务状态、参数操作与错误信息；最多保留最近 1000 条。", "muted"));
    top->addStretch();
    auto* copy = button("复制全部");
    auto* save = button("另存为…");
    auto* clear = button("清空日志", "danger");
    top->addWidget(copy);
    top->addWidget(save);
    top->addWidget(clear);
    content->addLayout(top);
    logView->setObjectName("eventLogView");
    logView->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont logFont("Microsoft YaHei UI");
    logFont.setPixelSize(13);
    logView->setFont(logFont);
    new EventLogHighlighter(logView);
    content->addWidget(logView, 1);
    outer->addWidget(panel, 1);

    QObject::connect(clear, &QPushButton::clicked, logView, &QPlainTextEdit::clear);
    QObject::connect(copy, &QPushButton::clicked, page,
                     [logView] { QApplication::clipboard()->setText(logView->toPlainText()); });
    QObject::connect(save, &QPushButton::clicked, page, [=] {
        const auto suggested = QDir(recorder->directory())
                                   .filePath("joint-tool_" + QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss") + ".log");
        const auto path = QFileDialog::getSaveFileName(page, i18n::text("保存事件日志"), suggested,
                                                       i18n::text("日志文件 (*.log *.txt)"));
        if (path.isEmpty())
            return;
        QFile file(path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream stream(&file);
            stream.setEncoding(QStringConverter::Utf8);
            stream << logView->toPlainText();
        }
    });
    return page;
}
} // 命名空间 studio::ui
