#include "qcustomplot.h"
#include "ui/PlotWidget.h"
#include "ui/ThemeColors.h"
#include <QApplication>
#include <QSignalSpy>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QtTest>
#include <algorithm>
#include <cmath>

using studio::ui::PlotWidget;

class PlotWidgetTests final : public QObject {
    Q_OBJECT
  private slots:
    void customThemePreservesSelectedColorAndReadableText() {
        using namespace studio::ui;
        for (const char* hex : {"#0b1019", "#22252b", "#ffffff", "#f1f4f8", "#fff1db",
                                "#b4d9f7", "#787878", "#ff0000", "#00ff00", "#0000ff"}) {
            const QColor selected(hex);
            const ThemeColors colors(selected);
            QCOMPARE(normalizedBackground(selected), selected);
            QCOMPARE(colors.window, selected);
            QVERIFY2(contrastRatio(colors.text, colors.window) >= 4.5, hex);
            QVERIFY2(contrastRatio(colors.text, colors.card) >= 4.5, hex);
            QVERIFY2(contrastRatio(colors.muted, colors.plot) >= 4.5, hex);
            QVERIFY2(contrastRatio(colors.actual, colors.plot) >= 3.0, hex);
            QVERIFY2(contrastRatio(colors.target, colors.plot) >= 3.0, hex);
        }
    }
    void targetAndFeedbackKeepTheirOwnTimestamps() {
        PlotWidget widget("角度", "角度 / rad");
        widget.resize(900, 280);
        widget.show();
        auto* plot = widget.findChild<QCustomPlot*>();
        QVERIFY(plot);

        widget.appendTarget(1.000, 0.25);
        widget.appendActual(1.012, 0.20);
        QCOMPARE(plot->graph(1)->dataCount(), 1);
        QCOMPARE(plot->graph(0)->dataCount(), 1);
        QCOMPARE(plot->graph(1)->dataMainKey(0), 1.000);
        QCOMPARE(plot->graph(0)->dataMainKey(0), 1.012);
        QVERIFY(plot->graph(1)->visible());
    }

    void stoppedAndRestartedStreamsAreNotJoinedByStraightLines() {
        PlotWidget widget("角度", "角度 / rad");
        widget.resize(900, 280);
        widget.show();
        auto* plot = widget.findChild<QCustomPlot*>();
        QVERIFY(plot);

        widget.appendActual(1.0, 0.4);
        widget.appendTarget(1.0, 0.5);
        widget.breakSeries();
        widget.appendActual(3.0, -0.4);
        widget.appendTarget(3.0, -0.5);

        QCOMPARE(plot->graph(0)->dataCount(), 3);
        QCOMPARE(plot->graph(1)->dataCount(), 3);
        QVERIFY(std::isnan(plot->graph(0)->dataMainValue(1)));
        QVERIFY(std::isnan(plot->graph(1)->dataMainValue(1)));
        QCOMPARE(plot->graph(0)->dataMainKey(1), 2.0);
        QCOMPARE(plot->graph(1)->dataMainKey(1), 2.0);
    }

    void legacyMotionWindowUsesThirtySecondsThreeSecondLeadAndFixedYMargin() {
        PlotWidget widget("扭矩", "扭矩 / N·m");
        widget.setRollingWindow(30.0, 3.0, 1.0);
        widget.resize(900, 280);
        widget.show();
        auto* plot = widget.findChild<QCustomPlot*>();
        QVERIFY(plot);

        QCOMPARE(plot->xAxis->range(), QCPRange(0.0, 33.0));
        widget.append(0.0, 10.0, 12.0, true);
        QTRY_COMPARE_WITH_TIMEOUT(plot->xAxis->range(), QCPRange(0.0, 3.0), 250);
        QCOMPARE(plot->yAxis->range(), QCPRange(9.0, 13.0));

        widget.append(10.0, -4.0, 2.0, true);
        QTRY_COMPARE_WITH_TIMEOUT(plot->xAxis->range(), QCPRange(0.0, 13.0), 250);
        QCOMPARE(plot->yAxis->range(), QCPRange(-5.0, 13.0));

        // 到 35 s 时，0 s 数据点已超出已部署程序的 30 s 保留窗口；
        // 第一个保留点成为 X 轴起点。
        widget.append(35.0, 5.0, 6.0, true);
        QTRY_COMPARE_WITH_TIMEOUT(plot->xAxis->range(), QCPRange(10.0, 38.0), 250);
        QCOMPARE(plot->graph(0)->dataMainKey(0), 10.0);
        QCOMPARE(plot->yAxis->range(), QCPRange(-5.0, 7.0));

        // 按真实 2 ms 运动节拍测试。长时间会话受 30 秒显示窗口限制，
        // 不会无限累积所有样本。
        for (int i = 1; i <= 100000; ++i) {
            const double x = 35.0 + i * 0.002;
            widget.append(x, std::sin(x), std::cos(x), true);
        }
        QTRY_VERIFY_WITH_TIMEOUT(plot->xAxis->range().upper >= 238.0, 500);
        QVERIFY(plot->graph(0)->dataCount() <= 15001);
        QVERIFY(plot->graph(0)->dataMainKey(0) >= 205.0);

        // 很大的绝对时间戳（30 天）不得扩大保留状态。
        constexpr double thirtyDays = 30.0 * 24.0 * 60.0 * 60.0;
        for (int i = 0; i <= 15000; ++i) {
            const double x = thirtyDays + i * 0.002;
            widget.append(x, std::sin(x), std::cos(x), true);
        }
        QTRY_VERIFY_WITH_TIMEOUT(plot->xAxis->range().upper >= thirtyDays + 33.0, 500);
        QVERIFY(plot->graph(0)->dataCount() <= 15001);
        QVERIFY(plot->graph(1)->dataCount() <= 15001);
        QVERIFY(plot->graph(0)->dataMainKey(0) >= thirtyDays);
        QVERIFY(plot->graph(1)->dataMainKey(0) >= thirtyDays);
    }

    void hiddenPlotDoesNotContinuouslyRepaint() {
        PlotWidget widget("角度", "角度 / rad");
        widget.setRollingWindow(30.0, 3.0, 1.0);
        auto* plot = widget.findChild<QCustomPlot*>();
        QVERIFY(plot);
        // 在测量控件隐藏期间追加数据造成的工作量前，
        // 先处理 resetView 触发的立即重绘。
        QApplication::processEvents();
        QSignalSpy replots(plot, &QCustomPlot::afterReplot);
        for (int i = 0; i < 10000; ++i)
            widget.append(i * 0.002, std::sin(i * 0.002));
        QTest::qWait(60);
        QCOMPARE(replots.count(), 0);
        widget.resize(900, 280);
        widget.show();
        QTRY_VERIFY_WITH_TIMEOUT(replots.count() > 0, 250);
        QVERIFY(widget.sampleCount() <= 10000);
    }

    void legacyCalibrationAndLinearityStylesArePreserved() {
        PlotWidget calibration("校准", "补偿值");
        calibration.setScatterOnly(true);
        auto* calibrationPlot = calibration.findChild<QCustomPlot*>();
        QVERIFY(calibrationPlot);
        QCOMPARE(calibrationPlot->graph(0)->lineStyle(), QCPGraph::lsNone);
        QCOMPARE(calibrationPlot->graph(0)->scatterStyle().shape(), QCPScatterStyle::ssCircle);

        PlotWidget linearity("线性度", "角度 / rad");
        linearity.setRollingWindow(30.0, 3.0, 0.5, 3.0);
        linearity.resize(900, 280);
        linearity.show();
        auto* linearityPlot = linearity.findChild<QCustomPlot*>();
        QVERIFY(linearityPlot);
        linearity.append(10.0, 2.0);
        QTRY_COMPARE_WITH_TIMEOUT(linearityPlot->xAxis->range(), QCPRange(10.0, 13.0), 250);
        QCOMPARE(linearityPlot->yAxis->range(), QCPRange(1.5, 5.0));
    }

    void stackedAxesStayAlignedAndNavigateIndependently() {
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        PlotWidget angle("角度", "角度 / rad");
        PlotWidget velocity("角速度", "角速度 / rad/s");
        PlotWidget torque("扭矩", "扭矩 / N·m");
        layout->addWidget(&angle);
        layout->addWidget(&velocity);
        layout->addWidget(&torque);
        host.resize(900, 650);
        host.show();
        QTest::qWait(40);

        const auto plots = host.findChildren<QCustomPlot*>();
        QCOMPARE(plots.size(), 3);
        plots[0]->yAxis->setRange(-0.116, -0.110);
        plots[1]->yAxis->setRange(-3, 3);
        plots[2]->yAxis->setRange(-1, 1);
        for (auto* plot : plots)
            plot->replot(QCustomPlot::rpImmediateRefresh);
        QTest::qWait(40);

        QCOMPARE(plots[0]->axisRect()->left(), plots[1]->axisRect()->left());
        QCOMPARE(plots[1]->axisRect()->left(), plots[2]->axisRect()->left());
        QCOMPARE(plots[0]->axisRect()->margins().left(), 80);

        angle.append(102, 1);
        velocity.append(102, 2);
        torque.append(102, 0.03);
        QTRY_VERIFY_WITH_TIMEOUT(std::all_of(plots.cbegin(), plots.cend(), [](const QCustomPlot* plot) {
            return plot->xAxis->range().upper > 102;
        }), 250);
        for (auto* plot : plots) {
            QCOMPARE(plot->xAxis->range().size(), 30.0);
            QCOMPARE(plot->xAxis->range().lower, 102.0);
        }

        // 普通单击不得停止实时视图。初始跨度填满后，左边缘保留在首个样本，
        // 水平范围扩展以保留历史。
        QTest::mouseClick(plots[0], Qt::LeftButton, {}, plots[0]->rect().center());
        angle.append(140, 0.5);
        velocity.append(140, -0.5);
        torque.append(140, 0.01);
        QTRY_VERIFY_WITH_TIMEOUT(std::all_of(plots.cbegin(), plots.cend(), [](const QCustomPlot* plot) {
            return plot->xAxis->range().upper > 140;
        }), 250);
        for (auto* plot : plots) {
            QCOMPARE(plot->xAxis->range().lower, 102.0);
            QVERIFY(plot->xAxis->range().size() > 30.0);
        }

        // 滚轮缩放同时改变两个坐标轴并进入持续的手动视图。
        // 新样本不得覆盖用户选择的范围；双击才明确恢复实时视图。
        const QCPRange xBeforeZoom = plots[0]->xAxis->range();
        const QCPRange velocityXBeforeZoom = plots[1]->xAxis->range();
        const QCPRange torqueXBeforeZoom = plots[2]->xAxis->range();
        const QCPRange yBeforeZoom = plots[0]->yAxis->range();
        const QCPRange velocityYBeforeZoom = plots[1]->yAxis->range();
        const QCPRange torqueYBeforeZoom = plots[2]->yAxis->range();
        const auto center = QPointF(plots[0]->axisRect()->rect().center());
        QWheelEvent wheel(center, QPointF(plots[0]->mapToGlobal(center.toPoint())), QPoint(), QPoint(0, -120),
                          Qt::NoButton, Qt::NoModifier, Qt::ScrollUpdate, false);
        QApplication::sendEvent(plots[0], &wheel);
        QApplication::processEvents();
        QVERIFY(!qFuzzyCompare(plots[0]->xAxis->range().size(), xBeforeZoom.size()));
        QVERIFY(!qFuzzyCompare(plots[0]->yAxis->range().size(), yBeforeZoom.size()));
        QCOMPARE(plots[1]->xAxis->range(), velocityXBeforeZoom);
        QCOMPARE(plots[2]->xAxis->range(), torqueXBeforeZoom);
        QCOMPARE(plots[1]->yAxis->range().lower, velocityYBeforeZoom.lower);
        QCOMPARE(plots[1]->yAxis->range().upper, velocityYBeforeZoom.upper);
        QCOMPARE(plots[2]->yAxis->range().lower, torqueYBeforeZoom.lower);
        QCOMPARE(plots[2]->yAxis->range().upper, torqueYBeforeZoom.upper);
        const QCPRange manualX = plots[0]->xAxis->range();
        const QCPRange manualY = plots[0]->yAxis->range();
        angle.append(141, 0.6);
        velocity.append(141, -0.6);
        torque.append(141, 0.02);
        QApplication::processEvents();
        QCOMPARE(plots[0]->xAxis->range(), manualX);
        QVERIFY(plots[1]->xAxis->range().upper > 141);
        QVERIFY(plots[2]->xAxis->range().upper > 141);
        QCOMPARE(plots[0]->yAxis->range().lower, manualY.lower);
        QCOMPARE(plots[0]->yAxis->range().upper, manualY.upper);

        QTest::mouseDClick(plots[0], Qt::LeftButton, {}, center.toPoint());
        QApplication::processEvents();

        // 长时间更新绝不能使 Y 轴范围指数膨胀。
        for (int i = 0; i < 3000; ++i) {
            const double x = 142.0 + i * 0.02;
            angle.append(x, std::sin(x));
            velocity.append(x, std::cos(x));
            torque.append(x, 0.02 * std::sin(x * 2));
        }
        const double latest = 142.0 + 2999 * 0.02;
        QTRY_VERIFY_WITH_TIMEOUT(std::all_of(plots.cbegin(), plots.cend(), [latest](const QCustomPlot* plot) {
            return plot->xAxis->range().upper > latest;
        }), 250);
        for (auto* plot : plots) {
            QVERIFY(plot->xAxis->range().upper > latest);
            QVERIFY(std::abs(plot->yAxis->range().lower) < 10);
            QVERIFY(std::abs(plot->yAxis->range().upper) < 10);
        }
        QVERIFY(plots[2]->yAxis->range().size() < plots[0]->yAxis->range().size());
        QVERIFY(plots[2]->yAxis->range().size() < plots[1]->yAxis->range().size());

        // 早于原滚动保留窗口的样本仍然可用。
        angle.append(500, 0.25);
        velocity.append(500, -0.25);
        torque.append(500, 0.005);
        QTRY_VERIFY_WITH_TIMEOUT(plots[0]->xAxis->range().upper > 500, 250);
        QCOMPARE(plots[0]->graph(0)->dataMainKey(0), 102.0);
        QCOMPARE(plots[1]->graph(0)->dataMainKey(0), 102.0);
        QCOMPARE(plots[2]->graph(0)->dataMainKey(0), 102.0);

        plots[1]->xAxis->setRange(47.25, 50.75);
        QApplication::processEvents();
        QCOMPARE(plots[1]->xAxis->range().lower, 47.25);
        QCOMPARE(plots[1]->xAxis->range().upper, 50.75);
        QVERIFY(plots[0]->xAxis->range().lower != 47.25);
        QVERIFY(plots[2]->xAxis->range().lower != 47.25);
        for (auto* plot : plots) {
            QVERIFY(plot->interactions().testFlag(QCP::iRangeDrag));
            QVERIFY(plot->interactions().testFlag(QCP::iRangeZoom));
        }
    }
    void rightDragSelectsAndMagnifiesBothAxes() {
        PlotWidget widget("角度", "角度 / rad");
        widget.resize(900, 280);
        widget.show();
        for (int i = 0; i <= 100; ++i)
            widget.append(i, i * 0.1);
        QTest::qWait(60);

        auto* plot = widget.findChild<QCustomPlot*>();
        QVERIFY(plot);
        plot->xAxis->setRange(0, 100);
        plot->yAxis->setRange(-10, 10);
        plot->replot(QCustomPlot::rpImmediateRefresh);

        const QRect area = plot->axisRect()->rect();
        const QPoint from(area.left() + area.width() / 4, area.top() + area.height() / 4);
        const QPoint to(area.left() + area.width() * 3 / 4, area.top() + area.height() * 3 / 4);
        QMouseEvent press(QEvent::MouseButtonPress, QPointF(from), QPointF(plot->mapToGlobal(from)),
                          Qt::RightButton, Qt::RightButton, Qt::NoModifier);
        QMouseEvent move(QEvent::MouseMove, QPointF(to), QPointF(plot->mapToGlobal(to)), Qt::NoButton,
                         Qt::RightButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(to), QPointF(plot->mapToGlobal(to)),
                            Qt::RightButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(plot, &press);
        QApplication::sendEvent(plot, &move);
        QApplication::sendEvent(plot, &release);
        QApplication::processEvents();

        QVERIFY(plot->xAxis->range().size() > 45.0);
        QVERIFY(plot->xAxis->range().size() < 55.0);
        QVERIFY(plot->yAxis->range().size() > 9.0);
        QVERIFY(plot->yAxis->range().size() < 11.0);

        const QCPRange selectedX = plot->xAxis->range();
        const QCPRange selectedY = plot->yAxis->range();
        widget.append(150, 15);
        QTest::qWait(40);
        QCOMPARE(plot->xAxis->range(), selectedX);
        QCOMPARE(plot->yAxis->range(), selectedY);
    }
    void doubleClickRestoresOnlyTheSelectedPlot() {
        QWidget host;
        auto* layout = new QVBoxLayout(&host);
        PlotWidget angle("角度", "角度 / rad");
        PlotWidget velocity("角速度", "角速度 / rad/s");
        PlotWidget torque("扭矩", "扭矩 / N·m");
        layout->addWidget(&angle);
        layout->addWidget(&velocity);
        layout->addWidget(&torque);
        host.resize(900, 650);
        host.show();
        QApplication::processEvents();

        const auto plots = host.findChildren<QCustomPlot*>();
        QCOMPARE(plots.size(), 3);
        for (auto* plot : plots) {
            plot->xAxis->setRange(80, 90);
            plot->yAxis->setRange(40, 50);
            plot->replot(QCustomPlot::rpImmediateRefresh);
        }

        QTest::mouseDClick(plots[1], Qt::LeftButton, {}, plots[1]->rect().center());
        QApplication::processEvents();
        QCOMPARE(plots[1]->xAxis->range().lower, 0.0);
        QCOMPARE(plots[1]->xAxis->range().upper, 30.0);
        QCOMPARE(plots[1]->yAxis->range().lower, -1.0);
        QCOMPARE(plots[1]->yAxis->range().upper, 1.0);
        for (int i : {0, 2}) {
            QCOMPARE(plots[i]->xAxis->range().lower, 80.0);
            QCOMPARE(plots[i]->xAxis->range().upper, 90.0);
            QCOMPARE(plots[i]->yAxis->range().lower, 40.0);
            QCOMPARE(plots[i]->yAxis->range().upper, 50.0);
        }

        torque.append(75, 0.4);
        for (auto* plot : plots) {
            plot->xAxis->setRange(5, 10);
            plot->yAxis->setRange(100, 120);
        }
        QTest::mouseDClick(plots[2], Qt::LeftButton, {}, plots[2]->rect().center());
        QApplication::processEvents();
        QVERIFY(plots[2]->xAxis->range().upper > 75);
        QCOMPARE(plots[0]->xAxis->range(), QCPRange(5, 10));
        QCOMPARE(plots[1]->xAxis->range(), QCPRange(5, 10));
        QCOMPARE(plots[0]->yAxis->range(), QCPRange(100, 120));
        QCOMPARE(plots[1]->yAxis->range(), QCPRange(100, 120));
        QVERIFY(plots[2]->yAxis->range().contains(0.4));
    }
};

QTEST_MAIN(PlotWidgetTests)
#include "PlotWidgetTests.moc"
