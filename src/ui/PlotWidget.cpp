#include "PlotWidget.h"
#include "Localization.h"
#include "Theme.h"
#include "qcustomplot.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

namespace studio::ui {
namespace {
class InteractivePlot final : public QCustomPlot {
  public:
    using QCustomPlot::QCustomPlot;
    std::function<void()> panStarted;
    std::function<void()> zoomFinished;
    std::function<void()> resetRequested;

  protected:
    void mousePressEvent(QMouseEvent* event) override {
        pressPosition_ = event->position();
        pressButton_ = event->button();
        panning_ = false;
        setSelectionRectMode(event->button() == Qt::RightButton ? QCP::srmZoom : QCP::srmNone);
        QCustomPlot::mousePressEvent(event);
    }
    void mouseMoveEvent(QMouseEvent* event) override {
        if (pressButton_ == Qt::LeftButton && !panning_ &&
            (event->position() - pressPosition_).manhattanLength() >= 3) {
            panning_ = true;
            if (panStarted)
                panStarted();
        }
        QCustomPlot::mouseMoveEvent(event);
    }
    void wheelEvent(QWheelEvent* event) override {
        QCustomPlot::wheelEvent(event);
        if (zoomFinished)
            zoomFinished();
    }
    void mouseReleaseEvent(QMouseEvent* event) override {
        QCustomPlot::mouseReleaseEvent(event);
        setSelectionRectMode(QCP::srmNone);
        if (pressButton_ == Qt::RightButton && zoomFinished)
            zoomFinished();
        pressButton_ = Qt::NoButton;
        panning_ = false;
    }
    void mouseDoubleClickEvent(QMouseEvent* event) override {
        setSelectionRectMode(QCP::srmNone);
        pressButton_ = Qt::NoButton;
        panning_ = false;
        QCustomPlot::mouseDoubleClickEvent(event);
        if (resetRequested)
            resetRequested();
        event->accept();
    }

  private:
    QPointF pressPosition_;
    Qt::MouseButton pressButton_ = Qt::NoButton;
    bool panning_ = false;
};
} // 匿名命名空间

PlotWidget::PlotWidget(QString title, QString unit, QWidget* parent) : QWidget(parent) {
    setObjectName("plotCard");
    // 三张运动曲线须适配 1100×700 窗口，并在大窗口中利用额外高度。
    setMinimumSize(300, 135);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(12, 9, 12, 8);
    outer->setSpacing(4);
    auto* header = new QHBoxLayout;
    auto* heading = new QLabel(i18n::text(title));
    heading->setObjectName("plotTitle");
    pauseBadge_ = new QLabel(i18n::text("显示已暂停"));
    pauseBadge_->setObjectName("plotPaused");
    pauseBadge_->hide();
    auto* help = new QLabel(i18n::text("左键平移 · 滚轮 XY 缩放 · 右键框选 XY · 双击恢复实时"));
    help->setObjectName("plotHelp");
    header->addWidget(heading);
    header->addStretch();
    header->addWidget(pauseBadge_);
    header->addWidget(help);
    outer->addLayout(header);

    auto* interactivePlot = new InteractivePlot;
    plot_ = interactivePlot;
    const auto beginManualNavigation = [this] {
        enterManualView();
    };
    interactivePlot->panStarted = beginManualNavigation;
    interactivePlot->zoomFinished = beginManualNavigation;
    interactivePlot->resetRequested = [this] { resetFromInteraction(); };
    plot_->setMinimumHeight(95);
    outer->addWidget(plot_, 1);
    actual_ = plot_->addGraph();
    target_ = plot_->addGraph();
    actual_->setName(i18n::text("反馈"));
    target_->setName(i18n::text("目标"));
    actual_->setAdaptiveSampling(true);
    target_->setAdaptiveSampling(true);
    actual_->setPen(QPen(QColor("#49dbbd"), 1.7));
    target_->setPen(QPen(QColor("#719dff"), 1.6, Qt::DashLine));
    target_->setVisible(false);
    target_->removeFromLegend();
    plot_->xAxis->setLabel(i18n::text("时间 / s"));
    plot_->yAxis->setLabel(i18n::text(unit));
    plot_->setInteractions(QCP::iRangeDrag | QCP::iRangeZoom);
    plot_->axisRect()->setRangeDrag(Qt::Horizontal | Qt::Vertical);
    plot_->axisRect()->setRangeZoom(Qt::Horizontal | Qt::Vertical);
    plot_->legend->setVisible(true);
    applyTheme();
    plot_->xAxis->setRange(0, window_);
    plot_->yAxis->setRange(-1, 1);

    // 绘图按旧程序的 20 ms 节奏刷新，与 2 ms 收发循环分离。
    repaintTimer_.setTimerType(Qt::PreciseTimer);
    repaintTimer_.setSingleShot(true);
    repaintTimer_.setInterval(20);
    connect(&repaintTimer_, &QTimer::timeout, this, [this] {
        if (dirty_ && !paused_ && isVisible()) {
            if (autoFollow_ && rangeDirty_) {
                updateRanges(latestX_);
                rangeDirty_ = false;
            }
            dirty_ = false;
            plot_->replot(QCustomPlot::rpQueuedReplot);
        }
    });
}

void PlotWidget::applyTheme() {
    const ThemeColors colors;
    plot_->setBackground(colors.card);
    plot_->axisRect()->setBackground(colors.plot);
    actual_->setPen(QPen(colors.actual, 1.7));
    target_->setPen(QPen(colors.target, 1.6, Qt::DashLine));
    const QPen axisPen(colors.muted);
    const QPen gridPen(colors.grid, 1, Qt::DotLine);
    for (auto* axis : {plot_->xAxis, plot_->yAxis, plot_->xAxis2, plot_->yAxis2}) {
        axis->setBasePen(axisPen);
        axis->setTickPen(axisPen);
        axis->setSubTickPen(axisPen);
        axis->setTickLabelColor(colors.muted);
        axis->setLabelColor(colors.text);
        axis->grid()->setPen(gridPen);
        axis->grid()->setSubGridPen(gridPen);
        axis->grid()->setZeroLinePen(QPen(colors.border));
    }
    // 固定左边距，避免 Y 轴刻度位数变化时三张图的 X 轴错位。
    plot_->axisRect()->setAutoMargins(QCP::msTop | QCP::msRight | QCP::msBottom);
    plot_->axisRect()->setMargins(QMargins(80, 0, 0, 0));
    plot_->legend->setBrush(colors.card);
    plot_->legend->setBorderPen(Qt::NoPen);
    plot_->legend->setTextColor(colors.text);
    plot_->legend->setFont(QFont("Microsoft YaHei UI", 8));
    plot_->axisRect()->insetLayout()->setInsetAlignment(0, Qt::AlignTop | Qt::AlignRight);
    plot_->selectionRect()->setPen(QPen(colors.accent, 1, Qt::DashLine));
    QColor selection = colors.accent;
    selection.setAlpha(35);
    plot_->selectionRect()->setBrush(selection);
}

void PlotWidget::refreshTheme() {
    applyTheme();
    plot_->replot(QCustomPlot::rpQueuedReplot);
}

void PlotWidget::append(double x, double y, double target, bool showTarget) {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(target))
        return;
    if (actual_->dataCount() && x < actual_->dataMainKey(actual_->dataCount() - 1))
        clear();
    actual_->addData(x, y);
    // 温度、校准和线性度等单曲线页面不保留不可见的零值目标序列。
    if (showTarget) {
        target_->addData(x, target);
        showTargetGraph();
    } else if (showTarget_) {
        target_->data()->clear();
        target_->removeFromLegend();
        target_->setVisible(false);
        showTarget_ = false;
    }
    finishAppend(x);
}

void PlotWidget::appendActual(double x, double y) {
    if (!std::isfinite(x) || !std::isfinite(y))
        return;
    if (actual_->dataCount() && x < actual_->dataMainKey(actual_->dataCount() - 1))
        clear();
    if (actualBreakPending_ && actual_->dataCount()) {
        const double previous = actual_->dataMainKey(actual_->dataCount() - 1);
        actual_->addData(previous + (x - previous) * 0.5, std::numeric_limits<double>::quiet_NaN());
    }
    actualBreakPending_ = false;
    actual_->addData(x, y);
    finishAppend(x);
}

void PlotWidget::appendTarget(double x, double target) {
    if (!std::isfinite(x) || !std::isfinite(target))
        return;
    if (target_->dataCount() && x < target_->dataMainKey(target_->dataCount() - 1))
        clear();
    if (targetBreakPending_ && target_->dataCount()) {
        const double previous = target_->dataMainKey(target_->dataCount() - 1);
        target_->addData(previous + (x - previous) * 0.5, std::numeric_limits<double>::quiet_NaN());
    }
    targetBreakPending_ = false;
    target_->addData(x, target);
    showTargetGraph();
    finishAppend(x);
}

void PlotWidget::breakSeries() {
    actualBreakPending_ = actual_->dataCount() > 0;
    targetBreakPending_ = target_->dataCount() > 0;
}

void PlotWidget::showTargetGraph() {
    if (!showTarget_) {
        target_->addToLegend();
        target_->setVisible(true);
        showTarget_ = true;
    }
}

void PlotWidget::finishAppend(double x) {
    if (rollingWindow_) {
        const double cutoff = x - window_;
        actual_->data()->removeBefore(cutoff);
        target_->data()->removeBefore(cutoff);
    }
    // 样本数只统计设备反馈；目标命令使用自己的时间轴，不增加反馈计数。
    sampleCount_ = actual_->dataCount();
    latestX_ = std::max(latestX_, x);
    rangeDirty_ = autoFollow_;
    dirty_ = true;
    scheduleReplot();
}

void PlotWidget::updateRanges(double latestX) {
    double firstX = 0.0;
    if (actual_->dataCount() && target_->dataCount())
        firstX = std::min(actual_->dataMainKey(0), target_->dataMainKey(0));
    else if (actual_->dataCount())
        firstX = actual_->dataMainKey(0);
    else if (target_->dataCount())
        firstX = target_->dataMainKey(0);
    const double lower = rollingWindow_ ? firstX : (firstX < 1.0 ? 0.0 : firstX);
    const double upper = rollingWindow_ ? latestX + trailingSeconds_
                                        : std::max(lower + window_, latestX + std::max(0.5, window_ * 0.05));
    plot_->xAxis->setRange(lower, upper);
    bool foundActual = false;
    bool foundTarget = false;
    auto range = actual_->getValueRange(foundActual, QCP::sdBoth, plot_->xAxis->range());
    if (showTarget_) {
        const auto targetRange = target_->getValueRange(foundTarget, QCP::sdBoth, plot_->xAxis->range());
        if (foundTarget) {
            if (foundActual)
                range.expand(targetRange);
            else
                range = targetRange;
        }
    }
    if (!foundActual && !foundTarget)
        return;
    const double magnitude = std::max(std::abs(range.lower), std::abs(range.upper));
    if (rollingWindow_) {
        plot_->yAxis->setRange(range.lower - rollingLowerYMargin_,
                               range.upper + rollingUpperYMargin_);
    } else {
        const double padding = range.size() >= 0.001 ? range.size() * 0.10
                                                     : std::max(0.05, magnitude * 0.05);
        plot_->yAxis->setRange(range.lower - padding, range.upper + padding);
    }
}

void PlotWidget::clear() {
    actual_->data()->clear();
    target_->data()->clear();
    sampleCount_ = 0;
    latestX_ = 0;
    actualBreakPending_ = targetBreakPending_ = false;
    rangeDirty_ = false;
    resetView();
}

void PlotWidget::setPaused(bool paused) {
    paused_ = paused;
    pauseBadge_->setText(i18n::text(paused ? "显示已暂停" : "手动浏览 · 双击恢复实时"));
    pauseBadge_->setVisible(paused || !autoFollow_);
    if (!paused) {
        dirty_ = true;
        scheduleReplot();
    }
}

void PlotWidget::setWindow(double seconds) {
    window_ = std::max(1.0, seconds);
    resetView();
}

void PlotWidget::setRollingWindow(double historySeconds, double trailingSeconds, double yMargin) {
    setRollingWindow(historySeconds, trailingSeconds, yMargin, yMargin);
}

void PlotWidget::setRollingWindow(double historySeconds, double trailingSeconds, double lowerYMargin,
                                  double upperYMargin) {
    window_ = std::max(1.0, historySeconds);
    trailingSeconds_ = std::max(0.0, trailingSeconds);
    rollingLowerYMargin_ = std::max(0.0, lowerYMargin);
    rollingUpperYMargin_ = std::max(0.0, upperYMargin);
    rollingWindow_ = true;
    resetView();
}

void PlotWidget::setScatterOnly(bool enabled) {
    actual_->setLineStyle(enabled ? QCPGraph::lsNone : QCPGraph::lsLine);
    actual_->setScatterStyle(enabled ? QCPScatterStyle(QCPScatterStyle::ssCircle, 3)
                                     : QCPScatterStyle::ssNone);
    dirty_ = true;
}

void PlotWidget::setAxisLabel(QString label) {
    plot_->xAxis->setLabel(i18n::text(label));
    dirty_ = true;
}

void PlotWidget::resetView() {
    autoFollow_ = true;
    if (actual_->dataCount()) {
        const double latest = actual_->dataMainKey(actual_->dataCount() - 1);
        latestX_ = latest;
        updateRanges(latest);
    } else {
        plot_->xAxis->setRange(0.0, window_ + (rollingWindow_ ? trailingSeconds_ : 0.0));
        plot_->yAxis->setRange(-1, 1);
    }
    rangeDirty_ = false;
    dirty_ = true;
    pauseBadge_->setText(i18n::text("显示已暂停"));
    pauseBadge_->setVisible(paused_);
    plot_->replot(QCustomPlot::rpImmediateRefresh);
}

void PlotWidget::enterManualView() {
    autoFollow_ = false;
    rangeDirty_ = false;
    pauseBadge_->setText(i18n::text("手动浏览 · 双击恢复实时"));
    pauseBadge_->show();
}

void PlotWidget::resetFromInteraction() {
    resetView();
}

void PlotWidget::scheduleReplot() {
    if (isVisible() && !repaintTimer_.isActive())
        repaintTimer_.start();
}

void PlotWidget::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
    dirty_ = true;
    rangeDirty_ = autoFollow_;
    scheduleReplot();
}
} // 命名空间 studio::ui
