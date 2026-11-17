#pragma once
#include <QTimer>
#include <QWidget>

class QCustomPlot;
class QCPGraph;
class QLabel;
class QShowEvent;

namespace studio::ui {
class PlotWidget final : public QWidget {
    Q_OBJECT
  public:
    explicit PlotWidget(QString title, QString unit, QWidget* parent = nullptr);
    void append(double x, double actual, double target = 0, bool showTarget = false);
    void appendActual(double x, double actual);
    void appendTarget(double x, double target);
    void breakSeries();
    void clear();
    void setPaused(bool);
    void setWindow(double seconds);
    void setRollingWindow(double historySeconds, double trailingSeconds, double yMargin);
    void setRollingWindow(double historySeconds, double trailingSeconds, double lowerYMargin,
                          double upperYMargin);
    void setScatterOnly(bool enabled);
    void setAxisLabel(QString label);
    int sampleCount() const { return sampleCount_; }
    void resetView();
    void refreshTheme();

  protected:
    void showEvent(QShowEvent* event) override;

  private:
    void applyTheme();
    void updateRanges(double latestX);
    void finishAppend(double x);
    void showTargetGraph();
    void enterManualView();
    void resetFromInteraction();
    void scheduleReplot();
    QCustomPlot* plot_ = nullptr;
    QCPGraph *actual_ = nullptr, *target_ = nullptr;
    QLabel* pauseBadge_ = nullptr;
    QTimer repaintTimer_;
    double window_ = 30, trailingSeconds_ = 3, rollingLowerYMargin_ = 1,
           rollingUpperYMargin_ = 1, latestX_ = 0;
    int sampleCount_ = 0;
    bool dirty_ = true, rangeDirty_ = false, paused_ = false, showTarget_ = false, autoFollow_ = true,
         rollingWindow_ = false;
    bool actualBreakPending_ = false, targetBreakPending_ = false;
};
} // 命名空间 studio::ui
