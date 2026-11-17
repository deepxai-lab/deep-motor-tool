#include "protocol/LegacyCanFdCodec.h"
#include "services/AsyncDeviceService.h"
#include "services/DeviceService.h"
#include "simulator/DeviceSimulatorServer.h"
#include "transport/UdpTransport.h"
#include "ui/PlotWidget.h"
#include "qcustomplot.h"
#include <QApplication>
#include <QSignalSpy>
#include <QVBoxLayout>
#include <QtTest>
#include <algorithm>
#include <cmath>
#include <memory>

using namespace studio;
using studio::ui::PlotWidget;

class UdpDeviceIntegrationTests final : public QObject {
    Q_OBJECT
  private slots:
    void asyncGuiPathKeepsTheCompleteCalibrationBurst() {
        DeviceSimulatorServer simulator;
        QVERIFY2(simulator.listen(QHostAddress::LocalHost, 43893),
                 "UDP simulator port 43893 is occupied");
        QSignalSpy protocolErrors(&simulator, &DeviceSimulatorServer::protocolError);

        AsyncDeviceService device(TransportKind::Udp);
        QSignalSpy reports(&device, &AsyncDeviceService::inputCalibrationReportReceived);
        QSignalSpy points(&device, &AsyncDeviceService::inputCalibrationPoint);
        device.connectSession({{"127.0.0.1", 0, 1}, 5000000,
                               "deep.ethercan.canfd.v1"});
        QTRY_VERIFY_WITH_TIMEOUT(device.connected(), 1000);

        device.calibrate(false);
        QTRY_VERIFY_WITH_TIMEOUT(device.busy(), 1000);
        QTRY_VERIFY_WITH_TIMEOUT(reports.count() >= 2 &&
                                     reports.last().at(0).toUInt() == 5u,
                                 5000);
        QVERIFY(!device.busy());
        QCOMPARE(points.count(), 256);
        QCOMPARE(reports.first().at(0).toUInt(), 0u);
        QCOMPARE(reports.last().at(0).toUInt(), 5u);
        QCOMPARE(protocolErrors.count(), 0);
    }

    void realUdpProtocolFeedsMotionAndLinearityCurves() {
        DeviceSimulatorServer simulator;
        QVERIFY2(simulator.listen(QHostAddress::LocalHost, 43893), "UDP simulator port 43893 is occupied");
        QSignalSpy configured(&simulator, &DeviceSimulatorServer::configured);
        QSignalSpy requests(&simulator, &DeviceSimulatorServer::requestReceived);
        QSignalSpy protocolErrors(&simulator, &DeviceSimulatorServer::protocolError);

        auto udp = std::make_unique<UdpTransport>(std::make_unique<LegacyCanFdCodec>());
        DeviceService device(std::move(udp), nullptr);
        QSignalSpy versions(&device, &DeviceService::versionChanged);
        QSignalSpy telemetry(&device, &DeviceService::telemetryReceived);
        QSignalSpy encoder(&device, &DeviceService::encoderPoint);
        QSignalSpy phaseAngle(&device, &DeviceService::phaseAngleReceived);
        QSignalSpy angleError(&device, &DeviceService::angleErrorReceived);
        QSignalSpy inputCalibrationCommands(&device, &DeviceService::inputCalibrationCommandResponse);
        QSignalSpy inputCalibrationReports(&device, &DeviceService::inputCalibrationReportReceived);
        QSignalSpy outputCalibrationCommands(&device, &DeviceService::outputCalibrationCommandResponse);
        QSignalSpy outputCalibrationResults(&device, &DeviceService::outputCalibrationResultReceived);
        QSignalSpy messages(&device, &DeviceService::message);

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
        connect(&device, &DeviceService::telemetryReceived, &host, [&](const Telemetry& sample) {
            const auto target = device.target();
            angle.append(sample.seconds, sample.position, target.position, true);
            velocity.append(sample.seconds, sample.velocity, target.velocity, true);
            torque.append(sample.seconds, sample.torque, target.torque, true);
        });

        device.connectSession({{"127.0.0.1", 0, 1}, 5000000, "deep.ethercan.canfd.v1"});
        QVERIFY(device.connecting() || device.connected());
        QTRY_COMPARE_WITH_TIMEOUT(configured.count(), 1, 1000);
        QTRY_VERIFY_WITH_TIMEOUT(device.connected(), 1000);
        QTRY_VERIFY_WITH_TIMEOUT(!versions.isEmpty() && versions.last().at(0).toString() == "V3-118", 1000);

        device.setEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(device.enabled(), 1000);
        MotionProgram program;
        program.waveform = Waveform::PositionSine;
        program.amplitude = 1;
        program.frequency = 0.5;
        program.target.kp = 10;
        program.target.kd = 0.5f;
        device.startMotion(program);

        QTRY_VERIFY_WITH_TIMEOUT(telemetry.count() >= 40, 2000);
        QTRY_VERIFY_WITH_TIMEOUT(angle.sampleCount() >= 40, 2000);
        QCOMPARE(angle.sampleCount(), velocity.sampleCount());
        QCOMPARE(velocity.sampleCount(), torque.sampleCount());
        const auto runningSample = qvariant_cast<Telemetry>(telemetry.last().at(0));
        QVERIFY(std::abs(runningSample.position) > 0.0001f);

        const int samplesBeforeStop = telemetry.count();
        device.stopMotion();
        QTRY_VERIFY_WITH_TIMEOUT(telemetry.count() > samplesBeforeStop, 1000);
        QTest::qWait(70); // 在 30 Hz 曲线范围与重绘阶段处理最后一帧反馈

        const auto plots = host.findChildren<QCustomPlot*>();
        QCOMPARE(plots.size(), 3);
        const auto last = qvariant_cast<Telemetry>(telemetry.last().at(0));
        QVERIFY(std::isfinite(last.position));
        QVERIFY(std::isfinite(last.velocity));
        QVERIFY(std::isfinite(last.torque));
        QVERIFY(plots[0]->yAxis->range().contains(last.position));
        QVERIFY(plots[1]->yAxis->range().contains(last.velocity));
        QVERIFY(plots[2]->yAxis->range().contains(last.torque));

        int motionRequests = 0;
        for (const auto& arguments : requests) {
            const auto frame = qvariant_cast<CanFrame>(arguments.at(0));
            if (frame.command == code(Command::Motion)) {
                ++motionRequests;
                QCOMPARE(frame.payload.size(), 16);
            }
        }
        QVERIFY2(motionRequests >= 40, "500 Hz motion scheduler did not produce enough UDP requests");

        device.startLinearity();
        QTRY_VERIFY_WITH_TIMEOUT(encoder.count() >= 10, 1000);
        device.stopLinearity();
        const int encoderCount = encoder.count();
        QTest::qWait(40);
        QCOMPARE(encoder.count(), encoderCount);

        device.setEnabled(false);
        QTRY_VERIFY_WITH_TIMEOUT(!device.enabled(), 1000);
        device.calibrate(false);
        QVERIFY(device.busy());
        QTRY_VERIFY_WITH_TIMEOUT(inputCalibrationReports.count() >= 2 &&
                                     inputCalibrationReports.last().at(0).toUInt() == 5u,
                                 4000);
        QVERIFY(!device.busy());
        QCOMPARE(inputCalibrationCommands.last().at(0).toUInt(), 0u);
        QCOMPARE(inputCalibrationReports.last().at(0).toUInt(), 5u);

        device.calibrate(true);
        QVERIFY(device.busy());
        QTRY_COMPARE_WITH_TIMEOUT(outputCalibrationResults.count(), 1, 1000);
        QVERIFY(!device.busy());
        QCOMPARE(outputCalibrationCommands.last().at(0).toUInt(), 0u);
        QCOMPARE(outputCalibrationResults.last().at(0).toUInt(), 0u);
        QCOMPARE(outputCalibrationResults.last().at(1).toUInt(), 0u);
        QCOMPARE(outputCalibrationResults.last().at(2).toUInt(), 0u);

        device.startPhaseAngleQuery();
        QTRY_VERIFY_WITH_TIMEOUT(phaseAngle.count() >= 5, 1000);
        device.stopPhaseAngleQuery();
        const int phaseCount = phaseAngle.count();
        QTest::qWait(30);
        QCOMPARE(phaseAngle.count(), phaseCount);

        device.startAngleErrorQuery();
        QTRY_VERIFY_WITH_TIMEOUT(angleError.count() >= 5, 1000);
        device.stopAngleErrorQuery();
        const int angleErrorCount = angleError.count();
        QTest::qWait(30);
        QCOMPARE(angleError.count(), angleErrorCount);
        QCOMPARE(protocolErrors.count(), 0);

        simulator.close();
        QTRY_VERIFY_WITH_TIMEOUT(device.connectionFailed(), 6000);
        QVERIFY(!device.connected());
        QVERIFY(std::any_of(messages.cbegin(), messages.cend(), [](const QList<QVariant>& arguments) {
            return arguments.at(0).toString().contains("通信中断");
        }));

        // 模拟适配器或设备拔出后恢复。同一程序、DeviceService 和 UdpTransport
        // 实例必须能够释放旧套接字、重新绑定并完成新的握手。
        QVERIFY2(simulator.listen(QHostAddress::LocalHost, 43893),
                 "UDP simulator could not rebind after reconnect");
        device.connectSession({{"127.0.0.1", 0, 1}, 5000000, "deep.ethercan.canfd.v1"});
        QTRY_COMPARE_WITH_TIMEOUT(configured.count(), 2, 1000);
        QTRY_VERIFY_WITH_TIMEOUT(device.connected(), 1000);
        QVERIFY(!device.connectionFailed());
        QVERIFY(!versions.isEmpty() && versions.last().at(0).toString() == "V3-118");
    }

    void missingDeviceEndsInVisibleFailureState() {
        auto udp = std::make_unique<UdpTransport>(std::make_unique<LegacyCanFdCodec>());
        DeviceService device(std::move(udp), nullptr);
        QSignalSpy messages(&device, &DeviceService::message);

        device.connectSession({{"127.0.0.1", 0, 1}, 5000000, "deep.ethercan.canfd.v1"});
        QVERIFY(device.connecting());
        QVERIFY(!device.connected());
        QTRY_VERIFY_WITH_TIMEOUT(device.connectionFailed(), 2500);
        QVERIFY(!device.connected());
        QVERIFY(std::any_of(messages.cbegin(), messages.cend(), [](const QList<QVariant>& arguments) {
            return arguments.at(0).toString().contains("通信建立失败");
        }));
    }
};

QTEST_MAIN(UdpDeviceIntegrationTests)
#include "UdpDeviceIntegrationTests.moc"
