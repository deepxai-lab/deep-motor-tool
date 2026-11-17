#include "services/FrameDiagnostics.h"
#include "ui/PageWidgets.h"
#include "ui/ThemeColors.h"
#include <QEvent>
#include <QAbstractTableModel>
#include <QBrush>
#include <QCheckBox>
#include <QDateTime>
#include <QFont>
#include <QHeaderView>
#include <QQueue>
#include <QTableView>
#include <QTimer>
#include <array>
#include <memory>

namespace studio::ui {
using namespace widgets;
namespace {

QString hexBytes(const QByteArray& bytes) {
    return QString::fromLatin1(bytes.toHex(' ').toUpper());
}

QString dlcText(int length) {
    static constexpr int lengths[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
    for (int dlc = 0; dlc < 16; ++dlc)
        if (lengths[dlc] == length)
            return QString("%1 / %2 B").arg(dlc).arg(length);
    return QString("— / %1 B").arg(length);
}

struct ResponseRow {
    std::array<QString, 8> cells;
};

class ResponseTableModel final : public QAbstractTableModel {
  public:
    explicit ResponseTableModel(QStringList headers, QObject* parent = nullptr)
        : QAbstractTableModel(parent), headers_(std::move(headers)) {
        if (parent)
            parent->installEventFilter(this);
    }

    bool eventFilter(QObject* watched, QEvent* event) override {
        if (event->type() == QEvent::PaletteChange) {
            colors_ = ThemeColors();
            if (!rows_.isEmpty())
                emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1),
                                 {Qt::ForegroundRole});
        }
        return QAbstractTableModel::eventFilter(watched, event);
    }

    int rowCount(const QModelIndex& = {}) const override { return rows_.size(); }
    int columnCount(const QModelIndex& = {}) const override { return headers_.size(); }
    QVariant data(const QModelIndex& index, int role) const override {
        if (!index.isValid() || index.row() >= rows_.size() || index.column() >= 8)
            return {};
        const auto& text = rows_[index.row()].cells[size_t(index.column())];
        if (role == Qt::DisplayRole || role == Qt::ToolTipRole)
            return text;
        if (role == Qt::TextAlignmentRole)
            return index.column() < 6 ? int(Qt::AlignCenter) : int(Qt::AlignLeft | Qt::AlignVCenter);
        if (role == Qt::ForegroundRole) {
            switch (index.column()) {
            case 0: return QBrush(colors_.muted);
            case 1:
            case 2:
            case 3: return QBrush(colors_.text);
            case 4: return QBrush(colors_.accent);
            case 5: return QBrush(colors_.muted);
            case 6: return QBrush(colors_.text);
            case 7:
                if (containsAny(text, {"失败", "异常", "错误", "超时", "丢失", "failed", "error", "timeout"}))
                    return QBrush(colors_.danger);
                if (containsAny(text, {"停止", "警告", "未知", "stopped", "warning", "unknown"}))
                    return QBrush(colors_.warning);
                return QBrush(colors_.accent);
            }
        }
        if (role == Qt::FontRole && (index.column() == 4 || index.column() == 6)) {
            QFont font(index.column() == 6 ? "Consolas" : "Microsoft YaHei UI");
            font.setPixelSize(13);
            font.setWeight(index.column() == 4 ? QFont::DemiBold : QFont::Normal);
            return font;
        }
        return {};
    }
    QVariant headerData(int section, Qt::Orientation orientation, int role) const override {
        if (role == Qt::DisplayRole && orientation == Qt::Horizontal && section < headers_.size())
            return headers_[section];
        return QAbstractTableModel::headerData(section, orientation, role);
    }
    void append(QList<ResponseRow> batch) {
        if (batch.isEmpty())
            return;
        beginResetModel();
        rows_.append(std::move(batch));
        constexpr int MaximumRows = 1000;
        if (rows_.size() > MaximumRows)
            rows_.remove(0, rows_.size() - MaximumRows);
        endResetModel();
    }
    void clear() {
        if (rows_.isEmpty())
            return;
        beginResetModel();
        rows_.clear();
        endResetModel();
    }

  private:
    ThemeColors colors_;
    static bool containsAny(const QString& text, std::initializer_list<const char*> words) {
        for (const auto* word : words)
            if (text.contains(QString::fromUtf8(word), Qt::CaseInsensitive))
                return true;
        return false;
    }
    QStringList headers_;
    QList<ResponseRow> rows_;
};

ResponseRow makeRow(const QDateTime& timestamp, const CanFrame& frame, const QString& boardIp) {
    return {{{timestamp.toString("yyyy-MM-dd HH:mm:ss.zzz"), boardIp,
              QString::number(frame.channel), QString::number(frame.motor),
              QString("0x%1  %2")
                  .arg(frame.command, 2, 16, QChar('0'))
                  .arg(FrameDiagnostics::commandName(frame.command, i18n::text)),
              dlcText(frame.payload.size()), hexBytes(frame.payload),
              FrameDiagnostics::summary(frame, i18n::text)}}};
}
} // 匿名命名空间

QWidget* createResponsePage(PageContext c) {
    auto* page = new QWidget;
    auto* outer = new QVBoxLayout(page);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(10);

    QVBoxLayout* content;
    auto* panel = card("电机应答数据", content);
    content->addWidget(label("显示通信板转发且与当前通信板、CAN 通道、电机 ID 匹配的应答帧。未知字段保留原始十六进制数据。", "muted"));

    auto* toolbar = new QHBoxLayout;
    auto* pause = new QCheckBox(i18n::text("暂停显示"));
    auto* autoScroll = new QCheckBox(i18n::text("自动滚动"));
    autoScroll->setChecked(true);
    auto* clear = button("清空列表");
    auto* status = label("已接收 0 帧 / 显示 0 行", "muted");
    toolbar->addWidget(pause);
    toolbar->addWidget(autoScroll);
    toolbar->addWidget(clear);
    toolbar->addStretch();
    toolbar->addWidget(status);
    content->addLayout(toolbar);

    const QStringList headers{i18n::text("时间"), i18n::text("通信板 IP"), "CAN",
                              i18n::text("电机 ID"), i18n::text("命令"),
                              i18n::text("DLC / 长度"), i18n::text("数据（HEX）"),
                              i18n::text("解析结果")};
    auto* model = new ResponseTableModel(headers, page);
    auto* table = new QTableView;
    table->setObjectName("responseTable");
    table->setModel(model);
    table->setAlternatingRowColors(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setWordWrap(false);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(30);
    table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setTextElideMode(Qt::ElideRight);
    table->setMouseTracking(true);
    auto* header = table->horizontalHeader();
    header->setSectionResizeMode(QHeaderView::Interactive);
    header->setStretchLastSection(false);
    const int widths[] = {185, 112, 52, 72, 170, 92, 390, 520};
    for (int column = 0; column < 8; ++column)
        table->setColumnWidth(column, widths[column]);
    content->addWidget(table, 1);
    outer->addWidget(panel, 1);

    auto received = std::make_shared<quint64>(0);
    auto pending = std::make_shared<QQueue<ResponseRow>>();
    QObject::connect(clear, &QPushButton::clicked, page, [=] {
        pending->clear();
        model->clear();
        status->setText(i18n::text("已接收 %1 帧 / 显示 0 行").arg(*received));
    });
    QObject::connect(c.device, &AsyncDeviceService::responseFrame, page, [=](const CanFrame& frame) {
        ++*received;
        if (!pause->isChecked()) {
            if (pending->size() >= 2000)
                pending->dequeue();
        const auto receivedAt = frame.receivedAt.isValid() ? frame.receivedAt
                                                           : QDateTime::currentDateTime();
        pending->enqueue(makeRow(receivedAt, frame, c.device->address().ip));
        }
    });
    auto* flush = new QTimer(page);
    flush->setInterval(100);
    QObject::connect(flush, &QTimer::timeout, page, [=] {
        // 其他页面处于活动状态时继续接收到有界队列，
        // 但不要长期持续修改隐藏的 QTableWidget。
        if (!page->isVisible())
            return;
        if (!pending->isEmpty()) {
            const int batchSize = qMin(200, pending->size());
            QList<ResponseRow> batch;
            batch.reserve(batchSize);
            for (int index = 0; index < batchSize; ++index)
                batch.append(pending->dequeue());
            model->append(std::move(batch));
            if (autoScroll->isChecked())
                table->scrollToBottom();
        }
        status->setText(i18n::text("已接收 %1 帧 / 显示 %2 行").arg(*received).arg(model->rowCount()));
    });
    flush->start();
    return page;
}
} // 命名空间 studio::ui
