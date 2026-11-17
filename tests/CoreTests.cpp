#include "protocol/LegacyCanFdCodec.h"
#include "protocol/CanFdFrameCodec.h"
#include "protocol/CommandCatalog.h"
#include "protocol/EtherCanV1Codec.h"
#include "protocol/ProtocolRegistry.h"
#include "protocol/StatusWordCatalog.h"
#include "services/DeviceRegistry.h"
#include "services/DeviceDiscovery.h"
#include "services/FrameDiagnostics.h"
#include "services/HostConfiguration.h"
#include "services/AsyncDeviceService.h"
#include "services/DeviceService.h"
#include "services/RecordingService.h"
#include "transport/UdpTransport.h"
#include "transport/TransportFactory.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QNetworkDatagram>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

using namespace studio;

class ScriptedTransport final : public Transport {
  public:
    bool open(const DeviceSession& session) override {
        address_ = session.address;
        open_ = true;
        return true;
    }
    void close() override { open_ = false; }
    bool isOpen() const override { return open_; }
    bool send(const CanFrame& frame) override {
        if (!open_)
            return false;
        sent_.append(frame);
        if (frame.command == code(Command::Version) && autoRespondToVersion_) {
            deliver(Command::Version, LegacyCanFdCodec::u16(3) + LegacyCanFdCodec::u16(118));
        }
        else if (frame.command == code(Command::Enable))
            deliver(Command::Enable, LegacyCanFdCodec::u16(0));
        else if (frame.command == code(Command::Disable))
            deliver(Command::Disable, QByteArray(1, 0));
        else if (frame.command == code(Command::CalibrateInput))
            deliver(Command::CalibrateInput, LegacyCanFdCodec::u16(0));
        else if (frame.command == code(Command::FirmwareStart))
            deliver(Command::FirmwareStart, QByteArray(1, 0));
        else if (frame.command == code(Command::FirmwareCheck))
            deliver(Command::FirmwareCheck, QByteArray(1, 0));
        return true;
    }
    const QList<CanFrame>& sent() const { return sent_; }
    void setAutoRespondToVersion(bool enabled) { autoRespondToVersion_ = enabled; }
    void deliver(Command command, QByteArray payload) {
        CanFrame frame;
        frame.board = quint8(address_.ip.section('.', 3).toUInt());
        frame.channel = address_.channel;
        frame.motor = address_.motor;
        frame.command = code(command);
        frame.payload = std::move(payload);
        emit frameReceived(frame);
    }

  private:
    DeviceAddress address_;
    QList<CanFrame> sent_;
    bool open_ = false;
    bool autoRespondToVersion_ = true;
};

class CoreTests : public QObject {
    Q_OBJECT
  private slots:
    void periodicQueryRatesComeFromIniFile() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto path = dir.filePath("config.ini");
        QSettings settings(path, QSettings::IniFormat);
        settings.setValue("periodic_queries/uvw_phase_angle_hz", 80);
        settings.setValue("periodic_queries/angle_error_hz", 125);
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);

        const auto rates = HostConfiguration::periodicQueryRates(path);
        QCOMPARE(rates.phaseAngleHz, 80);
        QCOMPARE(rates.angleErrorHz, 125);
    }

    void commandGoldenVector() {
        LegacyCanFdCodec codec;
        CanFrame f;
        f.command = 2;
        const auto bytes = codec.encode(f, 1);
        QCOMPARE(bytes.size(), 84);
        QCOMPARE(bytes.left(20).toHex(), QByteArray("4452010112a24a00ea0079004100000030000000"));
        QCOMPARE(bytes.mid(20), QByteArray(64, 0));
        f.motor = 15;
        f.command = 35;
        QCOMPARE(codec.encode(f, 1).mid(12, 2).toHex(), QByteArray("6f04"));
        f.channel = 4;
        QVERIFY(codec.encode(f, 1).isEmpty());
        f.channel = 0;
        f.command = 2;
        f.payload = QByteArray(1, 0);
        QVERIFY(codec.encode(f, 1).isEmpty());
        f.command = 30;
        f.payload.clear();
        QVERIFY(codec.encode(f, 1).isEmpty());
    }
    void downlinkContainerRoundTrip() {
        CanFrame request;
        request.board = 1;
        request.channel = 3;
        request.motor = 15;
        request.command = code(Command::Motion);
        request.payload = LegacyCanFdCodec::motion({1, -2, 0.5f, 12.3f, 0.7f});
        const auto encoded = CanFdFrameCodec::encodeTx(request);
        QCOMPARE(encoded.size(), CanFdFrameCodec::TxContainerBytes);
        CanFrame decoded;
        QString error;
        QVERIFY2(CanFdFrameCodec::decodeTx(encoded, decoded, error), qPrintable(error));
        QCOMPARE(decoded.board, request.board);
        QCOMPARE(decoded.channel, request.channel);
        QCOMPARE(decoded.motor, request.motor);
        QCOMPARE(decoded.command, request.command);
        QCOMPARE(decoded.payload, request.payload);
        auto invalid = encoded;
        invalid[2] = char(quint8(invalid[2]) | 0x10);
        QVERIFY(!CanFdFrameCodec::decodeTx(invalid, decoded, error));
    }
    void dataRateResponseAcceptsTheLegacyCanIdForm() {
        CanFrame response;
        response.board = 121;
        response.channel = 0;
        response.motor = 1;
        response.command = code(Command::SetCanFdDataRate);
        response.payload = QByteArray(1, char(8));
        const auto encoded = CanFdFrameCodec::encodeRx(response);
        QCOMPARE(encoded.mid(2, 4).toHex(), QByteArray("b1040000"));

        CanFrame decoded;
        QString error;
        QVERIFY2(CanFdFrameCodec::decodeRx(encoded, decoded, error), qPrintable(error));
        auto missingResponseBit = encoded;
        missingResponseBit[2] = char(quint8(missingResponseBit[2]) & ~quint8(0x10));
        QVERIFY2(CanFdFrameCodec::decodeRx(missingResponseBit, decoded, error), qPrintable(error));
        QCOMPARE(decoded.payload, response.payload);
    }
    void statusWordResponseAcceptsTheLegacyCanIdForm() {
        CanFrame response;
        response.board = 121;
        response.channel = 0;
        response.motor = 1;
        response.command = code(Command::Status);
        response.payload = LegacyCanFdCodec::u16(0x2004);
        const auto encoded = CanFdFrameCodec::encodeRx(response);

        CanFrame decoded;
        QString error;
        QVERIFY2(CanFdFrameCodec::decodeRx(encoded, decoded, error), qPrintable(error));
        QCOMPARE(decoded.payload, response.payload);
        auto missingResponseBit = encoded;
        missingResponseBit[2] = char(quint8(missingResponseBit[2]) & ~quint8(0x10));
        QVERIFY2(CanFdFrameCodec::decodeRx(missingResponseBit, decoded, error), qPrintable(error));
        QCOMPARE(decoded.payload, response.payload);
    }
    void commandCatalogIsCompleteAndExecutable() {
        QCOMPARE(commandSpecs().size(), size_t(40));
        for (const auto& spec : commandSpecs()) {
            QCOMPARE(commandSpec(code(spec.command)), &spec);
            if (spec.requestDlc) {
                const int bytes = canFdLength(*spec.requestDlc);
                QVERIFY(bytes >= 0);
                QVERIFY(validRequestPayload(code(spec.command), bytes));
                QVERIFY(!validRequestPayload(code(spec.command), bytes == 0 ? 1 : bytes - 1));
                CanFrame frame;
                frame.command = code(spec.command);
                if (spec.command == Command::GetCanId)
                    frame.motor = 0x1f;
                frame.payload = QByteArray(bytes, 0);
                QVERIFY2(!LegacyCanFdCodec{}.encode(frame, 0).isEmpty(), spec.symbol.data());
            }
        }
        QCOMPARE(canFdLength(9), 12);
        QCOMPARE(canFdLength(10), 16);
        QCOMPARE(canFdLength(11), 20);
        QCOMPARE(canFdLength(12), 24);
        QCOMPARE(canFdLength(13), 32);
        QCOMPARE(canFdLength(14), 48);
        QCOMPARE(canFdLength(15), 64);
        QCOMPARE(canFdDlcForLength(16), 10);
        QVERIFY(!validRequestPayload(code(Command::CalibrationReport), 0));
        QVERIFY(validResponsePayload(30, 7)); // 未知应答仍然可观察
        QVERIFY(!validResponsePayload(code(Command::Motion), 12));
        QVERIFY(validResponsePayload(code(Command::SetDriverTemperature), 1));
        QVERIFY(validResponsePayload(code(Command::SetDriverTemperature), 2));
        QVERIFY(validResponsePayload(code(Command::Version), 2));
        QVERIFY(validResponsePayload(code(Command::Version), 4));
        QVERIFY(validResponsePayload(code(Command::CalibrationReport), 8));
        QVERIFY(!validResponsePayload(code(Command::CalibrationReport), 2));
        QVERIFY(validResponsePayload(code(Command::FirmwareCheck), 1));
        QVERIFY(validResponsePayload(code(Command::FirmwareCheck), 2));
        QVERIFY(validResponsePayload(code(Command::FirmwareCheck), 3));
    }
    void statusWordCatalogMatchesV3Protocol() {
        QCOMPARE(statusBitSpecs().size(), size_t(16));
        for (quint8 bit = 0; bit < 16; ++bit) {
            const auto& spec = statusBitSpecs()[bit];
            QCOMPARE(static_cast<quint8>(spec.flag), bit);
            QVERIFY(!spec.symbol.empty());
            QVERIFY(hasStatusFlag(quint16(1) << bit, spec.flag));
            QVERIFY(!hasStatusFlag(0, spec.flag));
        }
        QVERIFY(!statusBitSpecs()[7].handledByHost);
        QVERIFY(statusBitSpecs()[8].handledByHost);
    }
    void v3CommandMatrixMatchesSuppliedProtocol() {
        struct Expected {
            quint8 command;
            quint8 requestDlc;
            std::optional<quint8> responseDlc;
            quint8 responseBytes;
        };
        const std::array expected{
            Expected{1, 0, 1, 1}, Expected{2, 0, 2, 2}, Expected{3, 0, 2, 2},
            Expected{4, 10, 10, 16}, Expected{5, 0, 1, 1}, Expected{6, 0, 1, 1},
            Expected{7, 4, 4, 4}, Expected{8, 1, 1, 1}, Expected{9, 1, 1, 1},
            Expected{10, 2, 2, 2}, Expected{11, 2, 2, 2}, Expected{12, 2, 2, 2},
            Expected{13, 2, 2, 2}, Expected{14, 2, 2, 2}, Expected{15, 2, 1, 2},
            Expected{16, 0, 1, 1}, Expected{17, 0, 1, 1}, Expected{18, 0, 1, 1},
            Expected{19, 8, 1, 1}, Expected{20, 8, 1, 1}, Expected{21, 0, std::nullopt, 1},
            Expected{22, 0, 4, 4}, Expected{23, 0, 2, 2}, Expected{24, 0, 11, 18},
            Expected{25, 0, 2, 2}, Expected{26, 0, 4, 4}, Expected{27, 0, 1, 1},
            Expected{28, 0, 0, 0},
            Expected{31, 0xff, 8, 8},
            Expected{37, 1, 1, 1}, Expected{38, 0, 8, 8}, Expected{39, 0, 10, 16},
            Expected{50, 0, 8, 8}, Expected{51, 0, 8, 8},
            Expected{52, 10, 11, 20}, Expected{53, 0, 8, 8},
        };
        for (const auto& item : expected) {
            const auto* spec = commandSpec(item.command);
            QVERIFY(spec);
            if (item.requestDlc == 0xff)
                QCOMPARE(spec->requestDlc, std::optional<quint8>{});
            else
                QCOMPARE(spec->requestDlc, std::optional<quint8>(item.requestDlc));
            QCOMPARE(spec->responseDlc, item.responseDlc);
            QCOMPARE(spec->responseSemanticBytes, item.responseBytes);
        }
    }
    void v3CanIdBroadcastUsesTheDocumentedSpecialTarget() {
        CanFrame request;
        request.board = 121;
        request.channel = 0;
        request.motor = 0x1f;
        request.command = code(Command::GetCanId);
        const auto encoded = CanFdFrameCodec::encodeTx(request);
        QVERIFY(!encoded.isEmpty());
        QCOMPARE(encoded.mid(2, 4).toHex(), QByteArray("9f030000"));

        CanFrame decoded;
        QString error;
        QVERIFY2(CanFdFrameCodec::decodeTx(encoded, decoded, error), qPrintable(error));
        QCOMPARE(decoded.motor, quint8(0x1f));
        QCOMPARE(decoded.command, code(Command::GetCanId));
    }
    void registryPersistence() {
        QTemporaryDir dir;
        const auto path = dir.filePath("devices.ini");
        {
            DeviceRegistry registry(path);
            QVERIFY(registry.addBoard({"192.168.2.88", 8000000}));
            QVERIFY(registry.addMotor({"192.168.2.88", 3, 0}));
            QVERIFY(registry.addMotor({"192.168.2.88", 3, 15}));
            QVERIFY(!registry.addMotor({"192.168.2.88", 3, 16}));
            QVERIFY(!registry.addMotor({"192.168.2.88", 3, 15}));
        }
        DeviceRegistry restored(path);
        QCOMPARE(restored.boards().last().dataBitrate, quint32(8000000));
        QCOMPARE(restored.motors().last().motor, quint8(15));
        QVERIFY(restored.removeBoard("192.168.2.88"));
        QCOMPARE(restored.motors().size(), 1);
    }
    void discoveryOrderMatchesV3AddressRange() {
        const auto candidates = DeviceDiscovery::candidates("192.168.1.121", 2);
        QCOMPARE(candidates.size(), 32);
        QCOMPARE(candidates.at(0).dataBitrate, quint32(5000000));
        QCOMPARE(candidates.at(0).address.motor, quint8(0));
        QCOMPARE(candidates.at(15).dataBitrate, quint32(5000000));
        QCOMPARE(candidates.at(15).address.motor, quint8(15));
        QCOMPARE(candidates.at(16).dataBitrate, quint32(8000000));
        QCOMPARE(candidates.at(16).address.motor, quint8(0));
        QCOMPARE(candidates.at(31).dataBitrate, quint32(8000000));
        QCOMPARE(candidates.at(31).address.motor, quint8(15));
        for (const auto& session : candidates) {
            QCOMPARE(session.address.ip, QString("192.168.1.121"));
            QCOMPARE(session.address.channel, quint8(2));
            QVERIFY(session.address.motor <= 15);
        }
    }
    void motionPrecision() {
        const auto b = LegacyCanFdCodec::motion({1, -2, 0.5f, 0.5f, 0.1f});
        QCOMPARE(b.toHex(), QByteArray("0000803f000000c00000003f00000000"));
        QCOMPARE(LegacyCanFdCodec::motion({0, 0, 0, 2.5f, 0.5f}).right(4).toHex(),
                 QByteArray("14000000"));
        QVERIFY(LegacyCanFdCodec::motion({0, 0, 0, -1, 0}).isEmpty());
        QCOMPARE(LegacyCanFdCodec::dataLength(10), 16);
        QCOMPARE(LegacyCanFdCodec::crc32("123456789"), quint32(0x89a1897f));
    }
    void firmwareUpdateMatchesDeployedProgram() {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QByteArray source = QByteArray::fromHex("0102030405060708090a0b0c0d");
        const auto path = directory.filePath("firmware.bin");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write(source), qint64(source.size()));
        file.close();

        auto transport = std::make_unique<ScriptedTransport>();
        auto* scripted = transport.get();
        DeviceService service(std::move(transport), nullptr);
        service.connectDevice({"192.168.1.121", 0, 1});
        QVERIFY(service.connected());
        QSignalSpy progress(&service, &DeviceService::firmwareProgress);
        const auto versionCount = [&] {
            return std::count_if(scripted->sent().cbegin(), scripted->sent().cend(),
                                 [](const CanFrame& frame) {
                return frame.command == code(Command::Version);
            });
        };
        const int versionsBeforeUpdate = versionCount();
        QElapsedTimer upgradeElapsed;
        upgradeElapsed.start();

        service.startFirmwareUpdate(path);
        QVERIFY(service.busy());
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 1000);
        QVERIFY(upgradeElapsed.elapsed() >= 180);
        QCOMPARE(versionCount(), versionsBeforeUpdate + 1);
        QVERIFY(!progress.isEmpty());
        QCOMPARE(progress.last().at(0).toInt(), 100);

        QList<CanFrame> firmwareFrames;
        for (const auto& frame : scripted->sent()) {
            if (frame.command >= code(Command::FirmwareStart) &&
                frame.command <= code(Command::FirmwareBoot))
                firmwareFrames.append(frame);
        }
        QCOMPARE(firmwareFrames.size(), 5);
        QCOMPARE(firmwareFrames.at(0).command, code(Command::FirmwareStart));
        QCOMPARE(firmwareFrames.at(0).payload, QByteArray());
        QCOMPARE(firmwareFrames.at(1).command, code(Command::FirmwareWrite));
        QCOMPARE(firmwareFrames.at(1).payload, source.left(8));
        QCOMPARE(firmwareFrames.at(2).command, code(Command::FirmwareWrite));
        const QByteArray padded = source + QByteArray(3, 0);
        QCOMPARE(firmwareFrames.at(2).payload, padded.mid(8, 8));
        QCOMPARE(firmwareFrames.at(3).command, code(Command::FirmwareCheck));
        QCOMPARE(firmwareFrames.at(3).payload,
                 LegacyCanFdCodec::u32(16) +
                     LegacyCanFdCodec::u32(LegacyCanFdCodec::crc32(padded)));
        QCOMPARE(firmwareFrames.at(4).command, code(Command::FirmwareBoot));
        QCOMPARE(firmwareFrames.at(4).payload, QByteArray());
    }
    void legacyProgramByteOrderIsFixed() {
        QCOMPARE(LegacyCanFdCodec::u16(0x1234).toHex(), QByteArray("3412"));
        QCOMPARE(LegacyCanFdCodec::u32(0x12345678).toHex(), QByteArray("78563412"));
        QCOMPARE(LegacyCanFdCodec::floats({1.0f, -2.0f}).toHex(),
                 QByteArray("0000803f000000c0"));

        LegacyCanFdCodec codec;
        CanFrame frame;
        frame.board = 121;
        frame.channel = 2;
        frame.motor = 15;
        frame.command = code(Command::Motion);
        frame.payload = LegacyCanFdCodec::motion({1, -2, 0.5f, 0.5f, 0.1f});
        const auto datagram = codec.encode(frame, 0x2a);
        QCOMPARE(datagram.left(10).toHex(), QByteArray("44522a0112a24a000203"));
        QCOMPARE(datagram.mid(12, 4).toHex(), QByteArray("8f000000"));
        QCOMPARE(datagram.mid(16, 4).toHex(), QByteArray("3a000000"));
        QCOMPARE(datagram.mid(20, 16).toHex(),
                 QByteArray("0000803f000000c00000003f00000000"));
    }
    void parameterDownlinkFramesMatchLegacyAbi() {
        struct Vector {
            Command command;
            QByteArray payload;
            QByteArray prefix;
        };
        const QList<Vector> vectors{
            {Command::SetGear, LegacyCanFdCodec::floats({9.0f}), "7900e10000003400000000001041"},
            {Command::SetBandwidth, LegacyCanFdCodec::u16(1000), "79004101000032000000e803"},
            {Command::SetTimeout, QByteArray(1, char(10)), "790021010000310000000a"},
            {Command::SetId, QByteArray(1, char(2)), "7900010100003100000002"},
            {Command::SetUnderVoltage, LegacyCanFdCodec::u16(181), "79008101000032000000b500"},
            {Command::SetOverVoltage, LegacyCanFdCodec::u16(601), "7900a1010000320000005902"},
            {Command::SetCurrent, LegacyCanFdCodec::u16(201), "79006101000032000000c900"},
            {Command::SetMotorTemperature, LegacyCanFdCodec::u16(851), "7900c1010000320000005303"},
            {Command::SetDriverTemperature, LegacyCanFdCodec::u16(801), "7900e1010000320000002103"},
            {Command::SaveConfig, {}, "79000102000030000000"},
        };
        for (const auto& vector : vectors) {
            CanFrame frame;
            frame.board = 121;
            frame.channel = 0;
            frame.motor = 1;
            frame.command = code(vector.command);
            frame.payload = vector.payload;
            const auto container = CanFdFrameCodec::encodeTx(frame);
            QCOMPARE(container.left(10 + vector.payload.size()).toHex(), vector.prefix);
            QCOMPARE(container.size(), CanFdFrameCodec::TxContainerBytes);
        }

        CanFrame gear{121, 0, 1, code(Command::SetGear), LegacyCanFdCodec::floats({9.0f})};
        const auto datagram = LegacyCanFdCodec{}.encode(gear, 1);
        QCOMPARE(datagram.left(24).toHex(),
                 QByteArray("4452010112a24a00df017900e10000003400000000001041"));
        QCOMPARE(datagram.size(), 84);
    }
    void receiveValidation() {
        LegacyCanFdCodec codec;
        CanFrame f;
        f.motor = 15;
        f.command = 4;
        f.payload =
            LegacyCanFdCodec::floats({1, 2, 3}) + LegacyCanFdCodec::u16(3245) + LegacyCanFdCodec::u16(2801);
        auto bytes = codec.encodeResponse({f, f});
        auto decoded = codec.decode(bytes);
        QVERIFY(decoded.ok());
        QCOMPARE(decoded.frames.size(), 2);
        QCOMPARE(decoded.frames.first().motor, quint8(15));
        const auto t = LegacyCanFdCodec::telemetry(decoded.frames.first().payload);
        QVERIFY(t);
        QCOMPARE(t->motorTemperature, 32.45f);
        for (int i = 0; i < bytes.size(); ++i)
            QVERIFY(!codec.decode(bytes.left(i)).ok());
        bytes[20] = char(bytes[20] ^ 1);
        QVERIFY(!codec.decode(bytes).ok());
        QVERIFY(!LegacyCanFdCodec::telemetry(QByteArray(15, 0)));
        f.payload = LegacyCanFdCodec::floats({1, 2, 3});
        QVERIFY(!codec.decode(codec.encodeResponse({f})).ok());

        f.payload = LegacyCanFdCodec::floats({1, 2, 3}) + LegacyCanFdCodec::u16(3245) +
                    LegacyCanFdCodec::u16(2801);
        auto invalidContainer = CanFdFrameCodec::encodeRx(f);
        invalidContainer[6] = 0; // 命令 4 使用 DLC 0 时无效
        const auto validContainer = CanFdFrameCodec::encodeRx(f);
        const auto mixedDatagram = EtherCanV1Codec::encode(
            invalidContainer + validContainer, EtherCanV1Codec::ReceiveCan, 1,
            EtherCanV1Codec::Response);
        const auto mixedDecoded = codec.decode(mixedDatagram);
        QVERIFY2(mixedDecoded.ok(), qPrintable(mixedDecoded.error));
        QCOMPARE(mixedDecoded.frames.size(), 1);
        QCOMPARE(mixedDecoded.frames.first().command, code(Command::Motion));
    }
    void realCalibrationUpstreamDoesNotRequireDownlinkFlags() {
        CanFrame report;
        report.board = 121;
        report.channel = 0;
        report.motor = 1;
        report.command = code(Command::CalibrationReport);
        report.payload = LegacyCanFdCodec::u16(2) + LegacyCanFdCodec::u16(0) +
                         LegacyCanFdCodec::floats({1.25f});

        auto container = CanFdFrameCodec::encodeRx(report);
        QVERIFY(!container.isEmpty());
        // 与已部署老程序接受的接收行为一致：不需要虚构的 bit4 应答标记，
        // 也不强制要求 BRS、CAN-FD 位。
        container[2] = char(quint8(container[2]) & ~0x10u);
        container[6] = char(8); // 只设置 DLC 8
        container[7] = container[8] = container[9] = 0;
        const auto datagram = EtherCanV1Codec::encode(
            container, EtherCanV1Codec::ReceiveCan, 0, EtherCanV1Codec::Response);

        const auto decoded = LegacyCanFdCodec{}.decode(datagram);
        QVERIFY2(decoded.ok(), qPrintable(decoded.error));
        QCOMPARE(decoded.frames.size(), 1);
        QCOMPARE(decoded.frames.first().command, code(Command::CalibrationReport));
        QCOMPARE(decoded.frames.first().motor, quint8(1));
        QCOMPARE(LegacyCanFdCodec::readU16(decoded.frames.first().payload, 0), quint16(2));
        QCOMPARE(LegacyCanFdCodec::readFloat(decoded.frames.first().payload, 4), 1.25f);

        for (const auto& response : QList<CanFrame>{
                 CanFrame{121, 0, 1, code(Command::Status), LegacyCanFdCodec::u16(0)},
                 CanFrame{121, 0, 1, code(Command::SetCanFdDataRate), QByteArray(1, char(8))}}) {
            auto responseContainer = CanFdFrameCodec::encodeRx(response);
            QVERIFY(!responseContainer.isEmpty());
            responseContainer[2] = char(quint8(responseContainer[2]) & ~0x10u);
            const auto responseDatagram = EtherCanV1Codec::encode(
                responseContainer, EtherCanV1Codec::ReceiveCan, 0,
                EtherCanV1Codec::Response);
            const auto responseDecoded = LegacyCanFdCodec{}.decode(responseDatagram);
            QVERIFY2(responseDecoded.ok(), qPrintable(responseDecoded.error));
            QCOMPARE(responseDecoded.frames.first().command, response.command);
        }

        CanFrame paddedMotion{121, 0, 1, code(Command::Motion),
                              LegacyCanFdCodec::floats({1, 2, 3}) +
                                  LegacyCanFdCodec::u16(20) + LegacyCanFdCodec::u16(0) +
                                  LegacyCanFdCodec::u32(0)};
        const auto paddedDatagram = EtherCanV1Codec::encode(
            CanFdFrameCodec::encodeRx(paddedMotion), EtherCanV1Codec::ReceiveCan, 0,
            EtherCanV1Codec::Response);
        const auto paddedDecoded = LegacyCanFdCodec{}.decode(paddedDatagram);
        QVERIFY2(paddedDecoded.ok(), qPrintable(paddedDecoded.error));
        QCOMPARE(paddedDecoded.frames.first().payload.size(), 20);

    }
    void configLayout() {
        LegacyCanFdCodec codec;
        auto bytes = codec.configuration(121, 5000000, 1);
        QCOMPARE(bytes.size(), 162);
        QCOMPARE(bytes.left(8).toHex(), QByteArray("4452010112a19800"));
        auto b = bytes.mid(10);
        QCOMPARE(b.mid(2, 8).toHex(), QByteArray("40420f00404b4c00"));
        QCOMPARE(quint8(b[23]), quint8(1));
        QCOMPARE(quint8(b[27]), quint8(1));
        QCOMPARE(quint8(b[39]), quint8(1));
        QVERIFY(codec.configuration(121, 123, 0).isEmpty());
        EtherCanPacket packet;
        QString error;
        QVERIFY(EtherCanV1Codec::decode(bytes, packet, error));
        QCOMPARE(packet.command, quint8(EtherCanV1Codec::ConfigureCan));
        QCOMPARE(packet.frameType, quint8(EtherCanV1Codec::Download));
        QCOMPARE(packet.body.size(), 152);
    }
    void udpLoopback() {
        QUdpSocket server;
        QVERIFY2(server.bind(QHostAddress::LocalHost, 43893), "Local test port 43893 is occupied");
        UdpTransport transport(std::make_unique<LegacyCanFdCodec>());
        QVERIFY(transport.open({{"127.0.0.1", 0, 1}, 5000000, "deep.ethercan.canfd.v1"}));
        QVERIFY(transport.localPort() != 0);
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingDatagrams(), 1000);
        auto configured = server.receiveDatagram();
        QCOMPARE(configured.data().size(), 162);
        QCOMPARE(quint8(configured.data()[2]), quint8(1));
        QCOMPARE(quint8(configured.data()[5]), quint8(EtherCanV1Codec::ConfigureCan));
        QSignalSpy spy(&transport, &Transport::frameReceived);
        CanFrame f;
        f.board = 1;
        f.command = 22;
        transport.send(f);
        QTRY_VERIFY_WITH_TIMEOUT(server.hasPendingDatagrams(), 1000);
        auto sent = server.receiveDatagram();
        QCOMPARE(sent.data().size(), 84);
        QCOMPARE(quint8(sent.data()[2]), quint8(1));
        LegacyCanFdCodec codec;
        f.payload = LegacyCanFdCodec::u16(3) + LegacyCanFdCodec::u16(118);
        auto response = codec.encodeResponse({f});
        server.writeDatagram(response, QHostAddress::LocalHost, transport.localPort());
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1000);
        QCOMPARE(qvariant_cast<CanFrame>(spy.takeFirst().at(0)).command, quint8(22));
        // 已部署网关可能从不同源端口应答。与老上位机一致，
        // 接收过滤条件为所选板卡 IP。
        QUdpSocket stranger;
        stranger.writeDatagram(response, QHostAddress::LocalHost, transport.localPort());
        QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1000);
        QCOMPARE(qvariant_cast<CanFrame>(spy.takeFirst().at(0)).command, quint8(22));
    }
    void simulationLifecycle() {
        DeviceService d;
        QSignalSpy samples(&d, &DeviceService::telemetryReceived);
        QSignalSpy replies(&d, &DeviceService::responseFrame);
        d.connectDevice({});
        QVERIFY(d.connected());
        QVERIFY(!d.enabled());
        QVERIFY(!replies.isEmpty());
        QCOMPARE(qvariant_cast<CanFrame>(replies.first().at(0)).command, code(Command::Version));
        d.setEnabled(true);
        QVERIFY(d.enabled());
        MotionProgram p;
        p.target.position = 2;
        p.target.kp = 0.5f;
        d.startMotion(p);
        QTRY_VERIFY_WITH_TIMEOUT(samples.count() > 4, 1000);
        QVERIFY(qvariant_cast<Telemetry>(samples.last().at(0)).position > 0);
        d.disconnectDevice();
        QVERIFY(!d.enabled());
        const auto count = samples.count();
        QTest::qWait(50);
        QCOMPARE(samples.count(), count);
    }
    void twoByteLegacyVersionReplyEstablishesTheSession() {
        auto transport = std::make_unique<ScriptedTransport>();
        auto* scripted = transport.get();
        scripted->setAutoRespondToVersion(false);
        DeviceService service(std::move(transport), nullptr);
        QSignalSpy versions(&service, &DeviceService::versionChanged);

        service.connectDevice({"192.168.1.121", 0, 1});
        QVERIFY(service.connecting());
        scripted->deliver(Command::Version, LegacyCanFdCodec::u16(614));

        QVERIFY(service.connected());
        QVERIFY(!versions.isEmpty());
        QCOMPARE(versions.last().at(0).toString(), QStringLiteral("V614-0"));
    }
    void faultAndDisconnectSendIndependentSafetyFrames() {
        auto transport = std::make_unique<ScriptedTransport>();
        auto* scripted = transport.get();
        DeviceService service(std::move(transport), nullptr);
        service.connectDevice({});
        QVERIFY(service.connected());
        service.setEnabled(true);
        QVERIFY(service.enabled());

        MotionProgram program;
        program.target.position = 1;
        program.target.kp = 10;
        service.startMotion(program);
        QVERIFY(service.motionActive());
        const int beforeFault = scripted->sent().size();
        scripted->deliver(Command::Status, LegacyCanFdCodec::u16(1));
        QVERIFY(!service.motionActive());
        QVERIFY(!service.enabled());
        const auto faultFrames = scripted->sent().sliced(beforeFault);
        QVERIFY(faultFrames.size() >= 2);
        QCOMPARE(faultFrames.at(0).command, code(Command::Motion));
        QCOMPARE(faultFrames.at(0).payload, LegacyCanFdCodec::motion({}));
        QCOMPARE(faultFrames.at(1).command, code(Command::Disable));

        service.setEnabled(true);
        QVERIFY(service.enabled());
        service.startMotion(program);
        QVERIFY(service.motionActive());
        const int beforeDisconnect = scripted->sent().size();
        service.disconnectDevice();
        const auto disconnectFrames = scripted->sent().sliced(beforeDisconnect);
        QVERIFY(disconnectFrames.size() >= 2);
        QCOMPARE(disconnectFrames.at(0).command, code(Command::Motion));
        QCOMPARE(disconnectFrames.at(0).payload, LegacyCanFdCodec::motion({}));
        QCOMPARE(disconnectFrames.at(1).command, code(Command::Disable));
        QCOMPARE(service.connectionState(), DeviceService::ConnectionState::Disconnected);
    }
    void inspectionStatusUsesTheDeployedEolBitMap() {
        auto transport = std::make_unique<ScriptedTransport>();
        auto* scripted = transport.get();
        DeviceService service(std::move(transport), nullptr);
        service.connectDevice({});
        QSignalSpy status(&service, &DeviceService::inspectionStatusWordChanged);

        scripted->deliver(Command::Status, LegacyCanFdCodec::u16(0x0080));
        QCOMPARE(status.last().at(1).toString(), QStringLiteral("异常: 输出端编码器校准失败"));
        scripted->deliver(Command::Status, LegacyCanFdCodec::u16(0x4000));
        QCOMPARE(status.last().at(1).toString(), QStringLiteral("异常: 角度误差过大"));
        scripted->deliver(Command::Status, LegacyCanFdCodec::u16(0x8000));
        QCOMPARE(status.last().at(1).toString(), QStringLiteral("状态正常"));
    }
    void inspectionPollingMatchesLegacyCommandOrder() {
        auto transport = std::make_unique<ScriptedTransport>();
        auto* scripted = transport.get();
        DeviceService service(std::move(transport), nullptr);
        service.connectDevice({});

        const std::array<float, 4> scope{0.01f, 0.1f, 0.02f, 0.2f};
        service.startInspection(scope);
        QCOMPARE(scripted->sent().last().command, code(Command::TestScope));
        QCOMPARE(scripted->sent().last().payload,
                 LegacyCanFdCodec::floats({scope[0], scope[1], scope[2], scope[3]}));

        scripted->deliver(Command::TestScope, QByteArray(1, 1));
        QCOMPARE(scripted->sent().last().command, code(Command::TestStart));
        QTRY_VERIFY_WITH_TIMEOUT(
            std::any_of(scripted->sent().cbegin(), scripted->sent().cend(), [](const CanFrame& frame) {
                return frame.command == code(Command::Status);
            }),
            1200);

        const auto stepCount = [scripted] {
            return std::count_if(scripted->sent().cbegin(), scripted->sent().cend(), [](const CanFrame& frame) {
                return frame.command == code(Command::TestStep);
            });
        };
        const int stepsBeforeAck = stepCount();
        scripted->deliver(Command::TestStart, QByteArray(1, 1));
        QTest::qWait(300);
        QCOMPARE(stepCount(), stepsBeforeAck);
        QTRY_VERIFY_WITH_TIMEOUT(stepCount() > stepsBeforeAck, 400);
        QTRY_VERIFY_WITH_TIMEOUT(
            std::any_of(scripted->sent().crbegin(), scripted->sent().crend(), [](const CanFrame& frame) {
                return frame.command == code(Command::TestData);
            }),
            700);
        service.stopInspection();
    }
    void stoppingInspectionDoesNotDisconnectWhileDriveMayStillBeTesting() {
        auto transport = std::make_unique<ScriptedTransport>();
        auto* scripted = transport.get();
        DeviceService service(std::move(transport), nullptr);
        service.connectDevice({});
        QVERIFY(service.connected());
        scripted->setAutoRespondToVersion(false);

        service.startInspection({0.01f, 0.1f, 0.02f, 0.2f});
        QVERIFY(service.busy());
        QTest::qWait(4200);
        service.stopInspection();
        QVERIFY(!service.busy());
        QTest::qWait(600);
        QVERIFY(service.connected());

        scripted->deliver(Command::Version,
                          LegacyCanFdCodec::u16(3) + LegacyCanFdCodec::u16(118));
        QVERIFY(service.connected());
    }
    void disableAndExplicitCancelReleaseCalibrationWait() {
        auto transport = std::make_unique<ScriptedTransport>();
        auto* scripted = transport.get();
        DeviceService service(std::move(transport), nullptr);
        service.connectDevice({});
        service.calibrate(false);
        QVERIFY(service.busy());
        service.cancelCalibration();
        QVERIFY(!service.busy());
        QCOMPARE(scripted->sent().last().command, code(Command::CalibrateInput));

        service.calibrate(false);
        QVERIFY(service.busy());
        service.setEnabled(false);
        QVERIFY(!service.busy());
        QVERIFY(!service.enabled());
        QCOMPARE(scripted->sent().last().command, code(Command::Disable));
    }
    void asynchronousCommunicationContinuesWhileUiThreadIsBlocked() {
        AsyncDeviceService service(TransportKind::Simulation);
        QSignalSpy samples(&service, &AsyncDeviceService::motionSampleReceived);
        service.connectDevice({});
        QTRY_VERIFY_WITH_TIMEOUT(service.connected(), 1000);
        service.setEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(service.enabled(), 1000);

        MotionProgram program;
        program.waveform = Waveform::PositionSine;
        program.amplitude = 1;
        program.frequency = 1;
        program.target.kp = 50;
        program.target.kd = 5;
        service.startMotion(program);
        QTRY_VERIFY_WITH_TIMEOUT(service.motionActive(), 1000);
        QTRY_VERIFY_WITH_TIMEOUT(samples.count() >= 3, 1000);

        samples.clear();
        // 本次休眠期间不处理界面事件。工作线程仍必须执行 2 ms 运动定时器、
        // 传输应答和帧解析器。
        QThread::msleep(100);
        QCOMPARE(samples.count(), 0);
        QTRY_VERIFY_WITH_TIMEOUT(samples.count() >= 10, 1000);

        service.stopMotion();
        QTRY_VERIFY_WITH_TIMEOUT(!service.motionActive(), 1000);
    }
    void asynchronousCalibrationPublishesIdleBeforeSuccess() {
        AsyncDeviceService service(TransportKind::Simulation);
        QSignalSpy reports(&service, &AsyncDeviceService::inputCalibrationReportReceived);
        QSignalSpy points(&service, &AsyncDeviceService::inputCalibrationPoint);
        QSignalSpy outputResults(&service, &AsyncDeviceService::outputCalibrationResultReceived);
        bool stepFiveObserved = false;
        bool busyWhenStepFiveReachedUi = true;
        connect(&service, &AsyncDeviceService::inputCalibrationReportReceived, this,
                [&](quint16 step, quint16, quint16, quint16) {
            if (step == 5) {
                stepFiveObserved = true;
                busyWhenStepFiveReachedUi = service.busy();
            }
        });

        service.connectDevice({});
        QTRY_VERIFY_WITH_TIMEOUT(service.connected(), 1000);
        service.calibrate(false);
        QTRY_VERIFY_WITH_TIMEOUT(service.busy(), 1000);
        QTRY_VERIFY_WITH_TIMEOUT(stepFiveObserved, 7000);

        QVERIFY(!busyWhenStepFiveReachedUi);
        QVERIFY(!service.busy());
        QCOMPARE(reports.count(), 2);
        QCOMPARE(reports.at(0).at(0).toUInt(), 0u);
        QCOMPARE(reports.at(1).at(0).toUInt(), 5u);
        QCOMPARE(points.count(), 256);
        QCOMPARE(outputResults.count(), 0);
    }
    void idleVersionProbeRunsAtTwentyHertzAndPausesDuringCalibration() {
        auto transport = std::make_unique<ScriptedTransport>();
        auto* scripted = transport.get();
        DeviceService service(std::move(transport), nullptr);
        service.connectDevice({});
        QVERIFY(service.connected());

        const auto versionCount = [scripted] {
            return std::count_if(scripted->sent().cbegin(), scripted->sent().cend(),
                                 [](const CanFrame& frame) {
                return frame.command == code(Command::Version);
            });
        };
        const int initial = versionCount();
        QTest::qWait(220);
        QVERIFY2(versionCount() - initial >= 3, "idle command-22 probe is not running at 20 Hz");

        service.calibrate(false);
        QVERIFY(service.busy());
        const int beforeBusinessWait = versionCount();
        QTest::qWait(180);
        QCOMPARE(versionCount(), beforeBusinessWait);

        scripted->deliver(Command::CalibrationReport,
                          LegacyCanFdCodec::u16(5) + QByteArray(6, 0));
        QVERIFY(!service.busy());
        QTest::qWait(180);
        QVERIFY(versionCount() - beforeBusinessWait >= 2);
    }
    void idleVersionProbePausesAndResumesForPeriodicBusinessStreams() {
        auto transport = std::make_unique<ScriptedTransport>();
        auto* scripted = transport.get();
        DeviceService service(std::move(transport), nullptr);
        service.connectDevice({});
        QVERIFY(service.connected());
        service.setEnabled(true);
        QVERIFY(service.enabled());

        const auto versionCount = [scripted] {
            return std::count_if(scripted->sent().cbegin(), scripted->sent().cend(),
                                 [](const CanFrame& frame) {
                return frame.command == code(Command::Version);
            });
        };
        const auto verifyPaused = [&] {
            const int before = versionCount();
            QTest::qWait(130);
            QCOMPARE(versionCount(), before);
        };
        const auto verifyResumed = [&] {
            const int before = versionCount();
            QTest::qWait(130);
            QVERIFY2(versionCount() - before >= 1,
                     "20 Hz idle command-22 probe did not resume after business ended");
        };

        MotionProgram motion;
        motion.waveform = Waveform::PositionSine;
        motion.amplitude = 0.5;
        motion.frequency = 1;
        motion.target.kp = 20;
        motion.target.kd = 2;
        service.startMotion(motion);
        QVERIFY(service.motionActive());
        verifyPaused();
        service.stopMotion();
        verifyResumed();

        service.startLinearity();
        QVERIFY(service.linearityActive());
        verifyPaused();
        service.stopLinearity();
        verifyResumed();

        service.startPhaseAngleQuery();
        QVERIFY(service.phaseAngleQueryActive());
        verifyPaused();
        service.stopPhaseAngleQuery();
        verifyResumed();

        service.startAngleErrorQuery();
        QVERIFY(service.angleErrorQueryActive());
        verifyPaused();
        service.stopAngleErrorQuery();
        verifyResumed();

        service.setEnabled(false);
        QVERIFY(!service.enabled());
        service.startInspection({0.01f, 0.1f, 0.02f, 0.2f});
        QVERIFY(service.busy());
        verifyPaused();
        service.stopInspection();
        QVERIFY(!service.busy());
        verifyResumed();
    }
    void acceptedReplyCarriesTheIoReceiveTimestamp() {
        auto transport = std::make_unique<ScriptedTransport>();
        auto* scripted = transport.get();
        DeviceService service(std::move(transport), nullptr);
        service.connectDevice({});
        QVERIFY(service.connected());
        QSignalSpy replies(&service, &DeviceService::responseFrame);

        const auto before = QDateTime::currentDateTime();
        scripted->deliver(Command::Status, LegacyCanFdCodec::u16(0));
        QCOMPARE(replies.count(), 1);
        const auto frame = qvariant_cast<CanFrame>(replies.first().at(0));
        QVERIFY(frame.receivedAt.isValid());
        QVERIFY(frame.receivedAt >= before);
        QVERIFY(frame.receivedAt <= QDateTime::currentDateTime());
    }
    void repeatedSessionLifecycleReturnsToAStableIdleState() {
        DeviceService d;
        for (int cycle = 0; cycle < 500; ++cycle) {
            d.connectDevice({"192.168.1.121", 0, 1});
            QVERIFY(d.connected());
            d.setEnabled(true);
            QVERIFY(d.enabled());
            d.setEnabled(false);
            QVERIFY(!d.enabled());
            d.disconnectDevice();
            QCOMPARE(d.connectionState(), DeviceService::ConnectionState::Disconnected);
            QVERIFY(!d.motionActive());
            QVERIFY(!d.linearityActive());
            QVERIFY(!d.busy());
        }
    }
    void motionCanRestartAfterStop() {
        DeviceService d;
        d.connectDevice({});
        QVERIFY(d.connected());
        d.setEnabled(true);
        QVERIFY(d.enabled());

        MotionProgram program;
        program.waveform = Waveform::PositionSine;
        program.amplitude = 1;
        program.frequency = 2;
        program.target.kp = 50;
        program.target.kd = 5;
        QSignalSpy targets(&d, &DeviceService::motionTargetSent);
        QSignalSpy states(&d, &DeviceService::motionStateChanged);

        d.startMotion(program);
        QVERIFY(d.motionActive());
        QTRY_VERIFY_WITH_TIMEOUT(targets.count() >= 5, 250);
        d.stopMotion();
        QVERIFY(!d.motionActive());
        QCOMPARE(states.last().at(0).toBool(), false);
        const int stoppedCount = targets.count();
        QTest::qWait(20);
        QCOMPARE(targets.count(), stoppedCount);

        d.startMotion(program);
        QVERIFY(d.motionActive());
        QCOMPARE(states.last().at(0).toBool(), true);
        QTRY_VERIFY_WITH_TIMEOUT(targets.count() >= stoppedCount + 5, 250);
        d.stopMotion();
    }

    void motionStopIsPublishedBeforeTerminalFeedback() {
        DeviceService d;
        d.connectDevice({});
        QTRY_VERIFY_WITH_TIMEOUT(d.connected(), 1000);
        d.setEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(d.enabled(), 1000);

        MotionProgram program;
        program.waveform = Waveform::PositionSine;
        program.amplitude = 1;
        program.frequency = 1;
        program.target.kp = 50;
        program.target.kd = 5;

        QStringList events;
        connect(&d, &DeviceService::motionStateChanged, this, [&events](bool active) {
            if (!active)
                events.append("stopped");
        });
        connect(&d, &DeviceService::telemetryReceived, this, [&events](const Telemetry&) {
            events.append("feedback");
        });

        d.startMotion(program);
        QTRY_VERIFY_WITH_TIMEOUT(events.count("feedback") >= 3, 1000);
        events.clear();
        d.stopMotion();

        QVERIFY(!events.isEmpty());
        QCOMPARE(events.first(), QString("stopped"));
    }

    void missingMotionFeedbackDoesNotStopTheLegacyFiveHundredHertzLoop() {
        auto transport = std::make_unique<ScriptedTransport>();
        auto* scripted = transport.get();
        DeviceService d(std::move(transport), nullptr);
        d.connectSession({{"192.168.1.121", 0, 1}, 5000000, "deep.ethercan.canfd.v1"});
        QVERIFY(d.connected());
        d.setEnabled(true);
        QVERIFY(d.enabled());

        MotionProgram program;
        program.waveform = Waveform::PositionSine;
        program.amplitude = 1;
        program.frequency = 1;
        program.target.kp = 50;
        program.target.kd = 5;
        d.startMotion(program);

        const auto motionCount = [&] {
            return std::count_if(scripted->sent().cbegin(), scripted->sent().cend(),
                                 [](const CanFrame& frame) {
                return frame.command == code(Command::Motion);
            });
        };
        QTRY_VERIFY_WITH_TIMEOUT(motionCount() >= 10, 300);
        QVERIFY(d.motionActive());
        QVERIFY(d.connected());
        QVERIFY(d.enabled());
        d.stopMotion();
    }

    void independentResetDoesNotStopOrRelabelTheMotionState() {
        auto transport = std::make_unique<ScriptedTransport>();
        auto* scripted = transport.get();
        DeviceService d(std::move(transport), nullptr);
        d.connectDevice({});
        QVERIFY(d.connected());
        d.setEnabled(true);
        QVERIFY(d.enabled());

        MotionProgram program;
        program.target.position = 0.25f;
        program.target.kp = 10;
        d.startMotion(program);
        QVERIFY(d.motionActive());
        d.command(Command::Reset);
        QCOMPARE(scripted->sent().last().command, code(Command::Reset));
        scripted->deliver(Command::Reset, QByteArray(1, 0));

        QVERIFY(d.motionActive());
        QVERIFY(d.enabled());
        d.stopMotion();
    }

    void periodicMotionUsesTheLegacyEnableTimeline() {
        DeviceService d;
        d.connectDevice({});
        QTRY_VERIFY_WITH_TIMEOUT(d.connected(), 1000);
        d.setEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(d.enabled(), 1000);
        QTest::qWait(40);

        MotionProgram program;
        program.waveform = Waveform::PositionSine;
        program.amplitude = 1.0;
        program.frequency = 0.7;
        program.target.kp = 50;
        program.target.kd = 5;
        QSignalSpy sent(&d, &DeviceService::motionTargetSent);

        d.startMotion(program);
        QTRY_VERIFY_WITH_TIMEOUT(sent.count() >= 5, 250);
        bool hasSubMillisecondTimestamp = false;
        constexpr double legacyPi = 3.1415926;
        const auto verifyLegacySample = [&](const QList<QVariant>& sample,
                                            const MotionProgram& expectedProgram) {
            const double seconds = sample.at(0).toDouble();
            const float legacySeconds = float(seconds);
            const float frequency = float(expectedProgram.frequency);
            const float amplitude = float(expectedProgram.amplitude);
            const float offset = float(expectedProgram.offset);
            const double phase = 2.0 * legacyPi * frequency * legacySeconds;
            const auto target = qvariant_cast<Motion>(sample.at(1));
            const float expectedPosition = float(amplitude * std::sin(phase) + offset);
            const float expectedVelocity =
                float(2.0 * legacyPi * frequency * amplitude * std::cos(phase));
            QVERIFY(std::abs(target.position - expectedPosition) < 1.0e-5f);
            QVERIFY(std::abs(target.velocity - expectedVelocity) < 1.0e-5f);
        };
        for (const auto& sample : sent) {
            const double seconds = sample.at(0).toDouble();
            const double milliseconds = seconds * 1000.0;
            if (std::abs(milliseconds - std::round(milliseconds)) > 1.0e-4)
                hasSubMillisecondTimestamp = true;
            verifyLegacySample(sample, program);
        }
        QVERIFY2(hasSubMillisecondTimestamp,
                 "500 Hz curve timestamps must not be truncated to integer milliseconds");
        const auto firstInitial = qvariant_cast<Motion>(sent.first().at(1));
        QVERIFY2(std::abs(firstInitial.position - program.offset) > 0.02f,
                 "a new sine run incorrectly reset the motor-enable timeline");

        const int beforeCount = sent.count();
        program.frequency = 1.4;
        d.startMotion(program);
        QTRY_VERIFY_WITH_TIMEOUT(sent.count() >= beforeCount + 10, 250);
        for (int i = beforeCount; i < sent.count(); ++i)
            verifyLegacySample(sent.at(i), program);
        d.stopMotion();
    }

    void motionFeedbackAndTargetUseOneReceiveTimeline() {
        DeviceService d;
        d.connectDevice({});
        QTRY_VERIFY_WITH_TIMEOUT(d.connected(), 1000);
        d.setEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(d.enabled(), 1000);

        MotionProgram program;
        program.waveform = Waveform::PositionSine;
        program.amplitude = 0.8;
        program.frequency = 1.2;
        program.offset = 0.15;
        program.target.kp = 50;
        program.target.kd = 5;
        QSignalSpy sent(&d, &DeviceService::motionTargetSent);
        QSignalSpy paired(&d, &DeviceService::motionSampleReceived);
        d.startMotion(program);
        QTRY_VERIFY_WITH_TIMEOUT(paired.count() >= 12, 500);
        QCoreApplication::processEvents();
        QCOMPARE(paired.count(), sent.count());

        for (int i = 0; i < paired.count(); ++i) {
            const auto feedback = qvariant_cast<Telemetry>(paired.at(i).at(0));
            const double targetSeconds = paired.at(i).at(1).toDouble();
            const auto pairedTarget = qvariant_cast<Motion>(paired.at(i).at(2));
            const auto sentTarget = qvariant_cast<Motion>(sent.at(i).at(1));
            QCOMPARE(targetSeconds, feedback.seconds);
            QCOMPARE(pairedTarget.position, sentTarget.position);
            QCOMPARE(pairedTarget.velocity, sentTarget.velocity);
            QCOMPARE(pairedTarget.torque, sentTarget.torque);
            QCOMPARE(pairedTarget.kp, sentTarget.kp);
            QCOMPARE(pairedTarget.kd, sentTarget.kd);
        }
        d.stopMotion();
    }

    void phaseAndAngleErrorQueriesMatchTheSuppliedProtocol() {
        auto transport = std::make_unique<ScriptedTransport>();
        auto* scripted = transport.get();
        DeviceService d(std::move(transport), nullptr);
        d.connectSession({{"192.168.1.121", 0, 1}, 5000000, "deep.ethercan.canfd.v1"});
        QTRY_VERIFY_WITH_TIMEOUT(d.connected(), 1000);

        QSignalSpy phaseData(&d, &DeviceService::phaseAngleReceived);
        QSignalSpy phaseState(&d, &DeviceService::phaseAngleQueryStateChanged);
        d.startPhaseAngleQuery();
        QVERIFY(d.phaseAngleQueryActive());
        QVERIFY(!phaseState.isEmpty() && phaseState.last().at(0).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(std::any_of(scripted->sent().cbegin(), scripted->sent().cend(),
                                             [](const CanFrame& frame) {
            return frame.command == code(Command::GetPhaseAngle) && frame.payload.isEmpty();
        }), 100);
        scripted->deliver(Command::GetPhaseAngle,
                          LegacyCanFdCodec::u32(1) + LegacyCanFdCodec::floats({-0.25f}));
        QTRY_COMPARE_WITH_TIMEOUT(phaseData.count(), 1, 100);
        const auto phase = qvariant_cast<PhaseAngleData>(phaseData.first().at(0));
        QCOMPARE(phase.phaseOrder, qint32(1));
        QCOMPARE(phase.electricalAngleOffset, -0.25f);
        d.stopPhaseAngleQuery();
        QVERIFY(!d.phaseAngleQueryActive());
        QVERIFY(!phaseState.last().at(0).toBool());

        QSignalSpy angleData(&d, &DeviceService::angleErrorReceived);
        QSignalSpy angleState(&d, &DeviceService::angleErrorQueryStateChanged);
        d.startAngleErrorQuery();
        QVERIFY(d.angleErrorQueryActive());
        QVERIFY(!angleState.isEmpty() && angleState.last().at(0).toBool());
        QTRY_VERIFY_WITH_TIMEOUT(std::any_of(scripted->sent().cbegin(), scripted->sent().cend(),
                                             [](const CanFrame& frame) {
            return frame.command == code(Command::GetAngleError) && frame.payload.isEmpty();
        }), 100);
        scripted->deliver(Command::GetAngleError,
                          LegacyCanFdCodec::floats({1.25f, 1.20f, 0.05f, 2.864789f}));
        QTRY_COMPARE_WITH_TIMEOUT(angleData.count(), 1, 100);
        const auto error = qvariant_cast<AngleErrorData>(angleData.first().at(0));
        QCOMPARE(error.referenceAngle, 1.25f);
        QCOMPARE(error.jointAngle, 1.20f);
        QCOMPARE(error.errorRadians, 0.05f);
        QCOMPARE(error.errorDegrees, 2.864789f);
        d.stopAngleErrorQuery();
        QVERIFY(!d.angleErrorQueryActive());
        QVERIFY(!angleState.last().at(0).toBool());
    }

    void canFdCounterQueryIsOneShotAndDecodesBothUint32Values() {
        auto transport = std::make_unique<ScriptedTransport>();
        auto* scripted = transport.get();
        DeviceService service(std::move(transport), nullptr);
        service.connectSession({{"192.168.1.121", 0, 1}, 5000000, "deep.ethercan.canfd.v1"});
        QVERIFY(service.connected());

        const auto requestCount = [&] {
            return std::count_if(scripted->sent().cbegin(), scripted->sent().cend(),
                                 [](const CanFrame& frame) {
                return frame.command == code(Command::GetCanFdTxRxCount);
            });
        };
        QSignalSpy counters(&service, &DeviceService::canFdCountersReceived);
        QCOMPARE(requestCount(), 0);
        service.queryCanFdCounters();
        QCOMPARE(requestCount(), 1);
        const auto& request = scripted->sent().last();
        QCOMPARE(request.command, code(Command::GetCanFdTxRxCount));
        QVERIFY(request.payload.isEmpty());

        scripted->deliver(Command::GetCanFdTxRxCount,
                          LegacyCanFdCodec::u32(0x89abcdefu) +
                              LegacyCanFdCodec::u32(0xfedcba98u));
        QCOMPARE(counters.count(), 1);
        const auto values = qvariant_cast<CanFdCounters>(counters.first().at(0));
        QCOMPARE(values.received, quint32(0x89abcdefu));
        QCOMPARE(values.transmitted, quint32(0xfedcba98u));

        QTest::qWait(30);
        QCOMPARE(requestCount(), 1);
        service.queryCanFdCounters();
        QCOMPARE(requestCount(), 2);
    }

    void canFdDataRateUsesCommand37AndValidatesTheReply() {
        auto transport = std::make_unique<ScriptedTransport>();
        auto* scripted = transport.get();
        DeviceService service(std::move(transport), nullptr);
        service.connectSession({{"192.168.1.121", 0, 1}, 5000000,
                                "deep.ethercan.canfd.v1"});
        QVERIFY(service.connected());

        QSignalSpy requests(&service, &DeviceService::canFdDataRateRequestSent);
        QSignalSpy replies(&service, &DeviceService::canFdDataRateResponse);
        const int before = scripted->sent().size();
        service.setCanFdDataRate(8);
        QCOMPARE(scripted->sent().size(), before + 1);
        const auto& request = scripted->sent().last();
        QCOMPARE(request.command, code(Command::SetCanFdDataRate));
        QCOMPARE(request.payload, QByteArray(1, char(8)));
        QCOMPARE(requests.count(), 1);
        QCOMPARE(requests.first().at(0).toUInt(), 8u);
        QVERIFY(requests.first().at(1).toBool());

        const int framesAfterRequest = scripted->sent().size();
        scripted->deliver(Command::SetCanFdDataRate, QByteArray(1, char(8)));
        QCOMPARE(replies.count(), 1);
        QCOMPARE(replies.first().at(0).toUInt(), 8u);
        QCOMPARE(replies.first().at(1).toUInt(), 8u);
        QVERIFY(replies.first().at(2).toBool());
        QCOMPARE(service.session().dataBitrate, quint32(5000000));
        QCOMPARE(scripted->sent().size(), framesAfterRequest);

        service.setCanFdDataRate(5);
        scripted->deliver(Command::SetCanFdDataRate, QByteArray(1, char(8)));
        QCOMPARE(replies.count(), 2);
        QCOMPARE(replies.last().at(0).toUInt(), 5u);
        QCOMPARE(replies.last().at(1).toUInt(), 8u);
        QVERIFY(!replies.last().at(2).toBool());

        const int validRequests = scripted->sent().size();
        service.setCanFdDataRate(7);
        QCOMPARE(scripted->sent().size(), validRequests);
        QCOMPARE(requests.count(), 3);
        QCOMPARE(requests.last().at(0).toUInt(), 7u);
        QVERIFY(!requests.last().at(1).toBool());
    }

    void motionAndPhaseQueryRemainIndependent() {
        auto transport = std::make_unique<ScriptedTransport>();
        auto* scripted = transport.get();
        DeviceService d(std::move(transport), nullptr);
        d.connectSession({{"192.168.1.121", 0, 1}, 5000000, "deep.ethercan.canfd.v1"});
        QVERIFY(d.connected());
        d.setEnabled(true);
        QVERIFY(d.enabled());

        MotionProgram program;
        program.waveform = Waveform::PositionSine;
        program.amplitude = 1;
        program.frequency = 1;
        program.target.kp = 50;
        program.target.kd = 5;
        d.startMotion(program);
        QVERIFY(d.motionActive());

        const auto phaseCount = [&] {
            return std::count_if(scripted->sent().cbegin(), scripted->sent().cend(),
                                 [](const CanFrame& frame) {
                return frame.command == code(Command::GetPhaseAngle);
            });
        };
        const int beforeStart = phaseCount();
        d.startPhaseAngleQuery();
        QCOMPARE(phaseCount(), beforeStart + 1);

        const auto motionCount = [&] {
            return std::count_if(scripted->sent().cbegin(), scripted->sent().cend(),
                                 [](const CanFrame& frame) {
                return frame.command == code(Command::Motion);
            });
        };
        const int motionBeforeWait = motionCount();
        QSignalSpy paired(&d, &DeviceService::motionSampleReceived);
        QSignalSpy feedbackFrames(&d, &DeviceService::telemetryReceived);
        QElapsedTimer elapsed;
        elapsed.start();
        QTRY_VERIFY_WITH_TIMEOUT(motionCount() >= motionBeforeWait + 5, 500);
        QTRY_VERIFY_WITH_TIMEOUT(phaseCount() > beforeStart + 1, 500);
        scripted->deliver(Command::Motion,
                          LegacyCanFdCodec::floats({0.25f, 1.5f, -0.1f}) +
                              LegacyCanFdCodec::u16(3200) + LegacyCanFdCodec::u16(3300));
        QTRY_COMPARE_WITH_TIMEOUT(feedbackFrames.count(), 1, 200);
        QCOMPARE(paired.count(), 1);
        const auto feedback = qvariant_cast<Telemetry>(feedbackFrames.first().at(0));
        const double receiveSeconds = elapsed.elapsed() / 1000.0;
        QVERIFY2(feedback.seconds >= receiveSeconds - 0.030,
                 "motion feedback did not keep its receive timestamp");
        QCOMPARE(paired.first().at(1).toDouble(), feedback.seconds);
        QVERIFY(!d.connectionFailed());
        QVERIFY(d.motionActive());
        d.stopPhaseAngleQuery();
        d.stopMotion();
    }

    void motionTimelineResetsOnEnableAndClear() {
        DeviceService d;
        QSignalSpy samples(&d, &DeviceService::telemetryReceived);
        d.connectDevice({});
        QTRY_VERIFY_WITH_TIMEOUT(d.connected(), 1000);
        QTest::qWait(60);
        d.setEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(d.enabled(), 1000);

        MotionProgram program;
        program.target.position = 1;
        program.target.kp = 10;
        d.startMotion(program);
        QTRY_VERIFY_WITH_TIMEOUT(samples.count() >= 3, 1000);
        const double afterEnable = qvariant_cast<Telemetry>(samples.last().at(0)).seconds;
        QVERIFY(afterEnable >= 0.0);
        QVERIFY(afterEnable < 0.25);

        QTest::qWait(80);
        const int beforeReset = samples.count();
        d.resetMotionTimeline();
        QTRY_VERIFY_WITH_TIMEOUT(samples.count() > beforeReset, 1000);
        const double afterClear = qvariant_cast<Telemetry>(samples.last().at(0)).seconds;
        QVERIFY(afterClear >= 0.0);
        QVERIFY(afterClear < 0.10);
        d.stopMotion();
    }

    void stoppedOrDisabledSimulationDoesNotInventMotionFrames() {
        DeviceService d;
        QSignalSpy stateSpy(&d, &DeviceService::stateChanged);
        QSignalSpy telemetrySpy(&d, &DeviceService::telemetryReceived);
        d.connectDevice({});
        QTRY_VERIFY_WITH_TIMEOUT(d.connected(), 1000);

        // 从未启动运动时反复使能、失能，不得发送全零 Motion 命令，
        // 也不得产生遥测或曲线点。
        for (int i = 0; i < 4; ++i) {
            d.setEnabled(true);
            QTRY_VERIFY_WITH_TIMEOUT(d.enabled(), 1000);
            d.setEnabled(false);
            QTRY_VERIFY_WITH_TIMEOUT(!d.enabled(), 1000);
        }
        d.stopMotion();
        QTest::qWait(100);
        QCOMPARE(telemetrySpy.count(), 0);

        d.setEnabled(true);
        QTRY_VERIFY_WITH_TIMEOUT(d.enabled(), 1000);

        MotionProgram program;
        program.waveform = Waveform::PositionSine;
        program.amplitude = 1.0;
        program.frequency = 0.7;
        program.target.kp = 50;
        program.target.kd = 5;
        d.startMotion(program);
        QTRY_VERIFY_WITH_TIMEOUT(telemetrySpy.count() >= 3, 1000);

        d.stopMotion();
        const int afterStop = telemetrySpy.count();
        QTest::qWait(100);
        QCOMPARE(telemetrySpy.count(), afterStop);

        d.startMotion(program);
        QTRY_VERIFY_WITH_TIMEOUT(telemetrySpy.count() > afterStop, 1000);
        d.setEnabled(false);
        QTRY_VERIFY_WITH_TIMEOUT(!d.enabled(), 1000);
        const int afterDisable = telemetrySpy.count();
        QTest::qWait(100);
        QCOMPARE(telemetrySpy.count(), afterDisable);
    }
    void frameDiagnosticsStayOutsideUi() {
        CanFrame frame;
        frame.command = code(Command::Version);
        frame.payload = LegacyCanFdCodec::u16(3) + LegacyCanFdCodec::u16(118);
        QCOMPARE(FrameDiagnostics::commandName(frame.command), QString("版本"));
        QCOMPARE(FrameDiagnostics::summary(frame), QString("版本 V3-118"));
        const FrameDiagnostics::TextResolver marked = [](const QString& source) { return "[" + source + "]"; };
        QCOMPARE(FrameDiagnostics::commandName(frame.command, marked), QString("[版本]"));
        QCOMPARE(FrameDiagnostics::summary(frame, marked), QString("[版本 V3-118]"));

        frame.command = code(Command::Status);
        frame.payload = LegacyCanFdCodec::u16((quint16(1) << 0) | (quint16(1) << 14));
        QCOMPARE(FrameDiagnostics::summary(frame),
                 QString("状态字 0x4001：过压保护，输出端编码器未校准"));
        QCOMPARE(FrameDiagnostics::statusWordFaults(0x2084, true),
                 QStringList({"过流保护 (0x0004)", "关节位置校验异常 (0x0080)",
                              "UVW 相线过流 (0x2000)"}));
        frame.payload = LegacyCanFdCodec::u16(0);
        QCOMPARE(FrameDiagnostics::summary(frame), QString("状态正常"));

        frame.command = code(Command::GetCalibration);
        frame.payload = LegacyCanFdCodec::u32(quint32(qint32(-1))) +
                        LegacyCanFdCodec::floats({0.5f});
        QCOMPARE(FrameDiagnostics::summary(frame), QString("电机相序 -1；电气角度 0.500000 rad"));

        frame.command = code(Command::GetJointStatus);
        frame.payload = LegacyCanFdCodec::u16(2) + LegacyCanFdCodec::u16(0x40) +
                        LegacyCanFdCodec::floats({48.25f});
        QCOMPARE(FrameDiagnostics::summary(frame),
                 QString("关节状态 2；故障码 0x0040；母线电压 48.25 V"));

        frame.command = code(Command::GetCanFdTxRxCount);
        frame.payload = LegacyCanFdCodec::u32(1234) + LegacyCanFdCodec::u32(5678);
        QCOMPARE(FrameDiagnostics::summary(frame), QString("CAN FD 接收 1234 帧；发送 5678 帧"));

        frame.command = code(Command::SetCanFdDataRate);
        frame.payload = QByteArray(1, char(8));
        QCOMPARE(FrameDiagnostics::summary(frame), QString("CAN FD数据波特率 8 Mbps"));
    }
    void extensionFactories() {
        QVERIFY(ProtocolRegistry::ids().contains("deep.ethercan.canfd.v1"));
        auto protocol = ProtocolRegistry::create("deep.ethercan.canfd.v1");
        QVERIFY(protocol);
        QCOMPARE(protocol->id(), QString("deep.ethercan.canfd.v1"));
        QVERIFY(!ProtocolRegistry::create("unknown.protocol"));

        auto simulation = TransportFactory::create(TransportKind::Simulation);
        QVERIFY(simulation);
        QVERIFY(simulation->open({}));
        QVERIFY(simulation->isOpen());
        simulation->close();
        QVERIFY(!simulation->isOpen());

        auto udp = TransportFactory::create(TransportKind::Udp, "deep.ethercan.canfd.v1");
        QVERIFY(udp);
        QVERIFY(!TransportFactory::create(TransportKind::Udp, "unknown.protocol"));
    }
    void parameterGroupsFollowLegacyDialog() {
        DeviceService d;
        d.connectDevice({});
        QSignalSpy replies(&d, &DeviceService::responseFrame);
        const QList<double> values{9, 1, 10, 1000, 20, 18, 60, 85, 80};

        d.writeParameters(values, ParameterGroup::Motor, false);
        QTRY_VERIFY_WITH_TIMEOUT(!d.busy(), 1500);
        QList<quint8> motorCommands;
        for (const auto& reply : replies) {
            const auto command = qvariant_cast<CanFrame>(reply.at(0)).command;
            if (command != code(Command::Version))
                motorCommands.append(command);
        }
        QCOMPARE(motorCommands, QList<quint8>({code(Command::SetGear), code(Command::SetBandwidth),
                                               code(Command::SetTimeout), code(Command::SetId)}));

        replies.clear();
        d.writeParameters(values, ParameterGroup::Protection, false);
        QTRY_VERIFY_WITH_TIMEOUT(!d.busy(), 1500);
        QList<quint8> protectionCommands;
        for (const auto& reply : replies) {
            const auto command = qvariant_cast<CanFrame>(reply.at(0)).command;
            if (command != code(Command::Version))
                protectionCommands.append(command);
        }
        QCOMPARE(protectionCommands,
                 QList<quint8>({code(Command::SetUnderVoltage), code(Command::SetOverVoltage),
                                code(Command::SetCurrent), code(Command::SetMotorTemperature),
                                 code(Command::SetDriverTemperature)}));
    }
    void motorParameterFramesMatchTheDeployedProgram() {
        DeviceService d;
        d.connectDevice({});
        QTRY_VERIFY_WITH_TIMEOUT(d.connected(), 1000);
        QSignalSpy replies(&d, &DeviceService::responseFrame);

        const QList<double> values{9, 2, 10, 1000, 20, 18, 60, 85, 80};
        d.writeParameters(values, ParameterGroup::Motor, true);
        QTRY_VERIFY_WITH_TIMEOUT(!d.busy(), 1500);
        QCOMPARE(d.address().motor, quint8(1));
        QList<quint8> commands;
        CanFrame setGearReply;
        CanFrame setBandwidthReply;
        CanFrame setTimeoutReply;
        CanFrame setIdReply;
        for (const auto& reply : replies) {
            const auto frame = qvariant_cast<CanFrame>(reply.at(0));
            if (frame.command == code(Command::Version))
                continue;
            commands.append(frame.command);
            if (frame.command == code(Command::SetGear))
                setGearReply = frame;
            if (frame.command == code(Command::SetBandwidth))
                setBandwidthReply = frame;
            if (frame.command == code(Command::SetTimeout))
                setTimeoutReply = frame;
            if (frame.command == code(Command::SetId))
                setIdReply = frame;
        }
        QCOMPARE(commands, QList<quint8>({code(Command::SetGear), code(Command::SetBandwidth),
                                         code(Command::SetTimeout), code(Command::SetId),
                                         code(Command::SaveConfig)}));
        QCOMPARE(setGearReply.payload.toHex(), QByteArray("00001041"));
        QCOMPARE(setBandwidthReply.payload.toHex(), QByteArray("e803"));
        QCOMPARE(setTimeoutReply.payload.toHex(), QByteArray("0a"));
        QCOMPARE(setIdReply.payload, QByteArray(1, char(2)));
    }
    void protectionParameterFramesUseLegacyTruncationAndOrder() {
        DeviceService d;
        d.connectDevice({});
        QTRY_VERIFY_WITH_TIMEOUT(d.connected(), 1000);
        QSignalSpy replies(&d, &DeviceService::responseFrame);

        const QList<double> values{9, 1, 10, 1000, 20.15, 18.15, 60.15, 85.15, 80.15};
        d.writeParameters(values, ParameterGroup::Protection, true);
        QTRY_VERIFY_WITH_TIMEOUT(!d.busy(), 1500);

        QList<quint8> commands;
        QList<QByteArray> payloads;
        for (const auto& reply : replies) {
            const auto frame = qvariant_cast<CanFrame>(reply.at(0));
            if (frame.command == code(Command::Version))
                continue;
            commands.append(frame.command);
            payloads.append(frame.payload);
        }
        QCOMPARE(commands, QList<quint8>({code(Command::SetUnderVoltage), code(Command::SetOverVoltage),
                                         code(Command::SetCurrent), code(Command::SetMotorTemperature),
                                         code(Command::SetDriverTemperature), code(Command::SaveConfig)}));
        QCOMPARE(payloads.at(0), LegacyCanFdCodec::u16(quint16(values[5] * 10)));
        QCOMPARE(payloads.at(1), LegacyCanFdCodec::u16(quint16(values[6] * 10)));
        QCOMPARE(payloads.at(2), LegacyCanFdCodec::u16(quint16(values[4] * 10)));
        QCOMPARE(payloads.at(3), LegacyCanFdCodec::u16(quint16(values[7] * 10)));
        QCOMPARE(payloads.at(4), LegacyCanFdCodec::u16(quint16(values[8] * 10)));
    }
    void parameterPersistenceRequiresSuccessfulCommand16Reply() {
        auto transport = std::make_unique<ScriptedTransport>();
        auto* scripted = transport.get();
        DeviceService service(std::move(transport), nullptr);
        QSignalSpy messages(&service, &DeviceService::message);
        service.connectDevice({});
        const QList<double> values{9, 1, 10, 1000, 20, 18, 60, 85, 80};

        service.writeParameters(values, ParameterGroup::Protection, true);
        QTRY_VERIFY_WITH_TIMEOUT(
            std::any_of(scripted->sent().cbegin(), scripted->sent().cend(),
                        [](const CanFrame& frame) {
                            return frame.command == code(Command::SaveConfig);
                        }),
            1200);
        QVERIFY(service.busy());
        QVERIFY(std::none_of(messages.cbegin(), messages.cend(), [](const QList<QVariant>& args) {
            return args.at(0).toString().contains(QStringLiteral("已写入设备"));
        }));

        scripted->deliver(Command::SaveConfig, QByteArray(1, 1));
        QVERIFY(!service.busy());
        QVERIFY(std::any_of(messages.cbegin(), messages.cend(), [](const QList<QVariant>& args) {
            return args.at(0).toString().contains(QStringLiteral("命令 16 返回码 1"));
        }));

        messages.clear();
        service.writeParameters(values, ParameterGroup::Protection, true);
        const int previousSaveCount = std::count_if(
            scripted->sent().cbegin(), scripted->sent().cend(), [](const CanFrame& frame) {
                return frame.command == code(Command::SaveConfig);
            });
        QTRY_VERIFY_WITH_TIMEOUT(
            std::count_if(scripted->sent().cbegin(), scripted->sent().cend(),
                          [](const CanFrame& frame) {
                              return frame.command == code(Command::SaveConfig);
                          }) > previousSaveCount,
            1200);
        scripted->deliver(Command::SaveConfig, QByteArray(1, 0));
        QVERIFY(!service.busy());
        QVERIFY(std::any_of(messages.cbegin(), messages.cend(), [](const QList<QVariant>& args) {
            return args.at(0).toString().contains(QStringLiteral("已写入设备"));
        }));
    }
    void calibrationAndInspection() {
        DeviceService d;
        d.connectDevice({});
        QSignalSpy inputReports(&d, &DeviceService::inputCalibrationReportReceived);
        QSignalSpy inputPoints(&d, &DeviceService::inputCalibrationPoint);
        QSignalSpy outputResults(&d, &DeviceService::outputCalibrationResultReceived);
        d.calibrate(false);
        QVERIFY(d.busy());
        d.setEnabled(true);
        QVERIFY(d.enabled());
        QVERIFY(!d.busy());
        QTRY_VERIFY_WITH_TIMEOUT(inputReports.count() >= 2 &&
                                     inputReports.last().at(0).toUInt() == 5u,
                                 7000);
        QCOMPARE(inputPoints.count(), 256);
        QVERIFY(!d.busy());
        d.setEnabled(false);
        QVERIFY(!d.enabled());
        d.calibrate(true);
        QVERIFY(d.busy());
        QTRY_COMPARE_WITH_TIMEOUT(outputResults.count(), 1, 1000);
        QVERIFY(!d.busy());
        QCOMPARE(outputResults.last().at(0).toUInt(), 0u);
        QCOMPARE(outputResults.last().at(1).toUInt(), 0u);
        QCOMPARE(outputResults.last().at(2).toUInt(), 0u);
        QSignalSpy test(&d, &DeviceService::inspectionFinished);
        QSignalSpy inspectionStatus(&d, &DeviceService::inspectionStatusChanged);
        QSignalSpy statusWord(&d, &DeviceService::inspectionStatusWordChanged);
        QSignalSpy inspectionData(&d, &DeviceService::inspectionDataReceived);
        d.startInspection({0.01f, 0.1f, 0.01f, 0.1f});
        QTRY_COMPARE_WITH_TIMEOUT(test.count(), 1, 6000);
        QVERIFY(inspectionStatus.count() >= 2);
        QCOMPARE(inspectionStatus.at(0).at(0).toString(), QString("设置成功"));
        QCOMPARE(inspectionStatus.at(1).at(0).toString(), QString("开始测试"));
        QVERIFY(statusWord.count() >= 2);
        QVERIFY(inspectionData.count() >= 1);
        QVERIFY(!d.busy());
    }
    void calibrationStagesUseLegacyOffsetsAndDoNotFalseDisconnect() {
        auto transport = std::make_unique<ScriptedTransport>();
        auto* scripted = transport.get();
        DeviceService d(std::move(transport), nullptr);
        d.connectDevice({});
        QVERIFY(d.connected());
        QSignalSpy points(&d, &DeviceService::inputCalibrationPoint);
        QSignalSpy commandResponses(&d, &DeviceService::inputCalibrationCommandResponse);
        QSignalSpy reports(&d, &DeviceService::inputCalibrationReportReceived);
        QSignalSpy outputResults(&d, &DeviceService::outputCalibrationResultReceived);
        d.calibrate(false);
        QVERIFY(d.busy());
        QCOMPARE(scripted->sent().last().command, quint8(3));
        QCOMPARE(commandResponses.count(), 1);
        QCOMPARE(commandResponses.at(0).at(0).toUInt(), 0u);
        QVERIFY(d.busy());

        // 驱动器内部校准启动时可能暂时无应答。
        // 通用 4.5 s 空闲看门狗不得终止该工作流。
        QTest::qWait(4700);
        QVERIFY(d.connected());
        QVERIFY(d.busy());

        // 命令 31 的 step 6 始终按独立输出轴协议解析，
        // 但不得释放正在运行的输入轴校准。
        scripted->deliver(Command::CalibrationReport,
                          LegacyCanFdCodec::u16(6) + QByteArray(6, 0));
        QCOMPARE(outputResults.count(), 1);
        QVERIFY(d.busy());
        QCOMPARE(reports.count(), 0);
        QCOMPARE(points.count(), 0);

        scripted->deliver(Command::CalibrationReport,
                          LegacyCanFdCodec::u16(0) + QByteArray(6, 0));
        scripted->deliver(Command::CalibrationReport,
                          LegacyCanFdCodec::u16(1) + LegacyCanFdCodec::u16(0) +
                              LegacyCanFdCodec::u16(0) + LegacyCanFdCodec::u16(0));
        scripted->deliver(Command::CalibrationReport,
                          LegacyCanFdCodec::u16(2) + LegacyCanFdCodec::u16(0) +
                              LegacyCanFdCodec::floats({1.25f}));
        scripted->deliver(Command::CalibrationReport,
                          LegacyCanFdCodec::u16(3) + LegacyCanFdCodec::u16(0) +
                              LegacyCanFdCodec::u32(quint32(qint32(-123456))));
        scripted->deliver(Command::CalibrationReport,
                          LegacyCanFdCodec::u16(4) + LegacyCanFdCodec::floats({0.75f}) +
                              LegacyCanFdCodec::u16(0));
        scripted->deliver(Command::CalibrationReport,
                          LegacyCanFdCodec::u16(5) + QByteArray(6, 0));

        QCOMPARE(points.count(), 2);
        QCOMPARE(points.at(0).at(0).toInt(), 0);
        QCOMPARE(points.at(0).at(1).toDouble(), 1.25);
        QCOMPARE(points.at(1).at(0).toInt(), 1);
        QCOMPARE(points.at(1).at(1).toDouble(), -123456.0);
        QCOMPARE(reports.count(), 3);
        QCOMPARE(reports.at(0).at(0).toUInt(), 0u);
        QCOMPARE(reports.at(1).at(0).toUInt(), 1u);
        QCOMPARE(reports.at(1).at(2).toUInt(), 0u);
        QCOMPARE(reports.last().at(0).toUInt(), 5u);
        QCOMPARE(reports.last().at(1).toUInt(), 0u);
        QCOMPARE(reports.last().at(2).toUInt(), 0u);
        QCOMPARE(reports.last().at(3).toUInt(), 0u);
        QVERIFY(!d.busy());
    }
    void outputCalibrationRequiresRealStepSixResult() {
        auto transport = std::make_unique<ScriptedTransport>();
        auto* scripted = transport.get();
        DeviceService d(std::move(transport), nullptr);
        d.connectDevice({});
        QVERIFY(d.connected());
        QSignalSpy commandResponses(&d, &DeviceService::outputCalibrationCommandResponse);
        QSignalSpy reports(&d, &DeviceService::outputCalibrationResultReceived);
        QSignalSpy inputReports(&d, &DeviceService::inputCalibrationReportReceived);
        QSignalSpy points(&d, &DeviceService::inputCalibrationPoint);
        QSignalSpy messages(&d, &DeviceService::message);

        d.calibrate(true);
        QVERIFY(d.busy());
        QCOMPARE(scripted->sent().last().command, quint8(25));
        QVERIFY(scripted->sent().last().payload.isEmpty());
        scripted->deliver(Command::CalibrateOutput, LegacyCanFdCodec::u16(0));
        QVERIFY(d.busy());
        QCOMPARE(commandResponses.count(), 1);
        QCOMPARE(commandResponses.last().at(0).toUInt(), 0u);

        // 输入轴命令 31 帧仍通过独立协议信号可观察，
        // 但不得更新或释放输出轴工作流。
        scripted->deliver(Command::CalibrationReport,
                          LegacyCanFdCodec::u16(0) + QByteArray(6, 0));
        scripted->deliver(Command::CalibrationReport,
                          LegacyCanFdCodec::u16(2) + LegacyCanFdCodec::u16(0) +
                              LegacyCanFdCodec::floats({1.25f}));
        scripted->deliver(Command::CalibrationReport,
                          LegacyCanFdCodec::u16(5) + QByteArray(6, 0));
        QVERIFY(d.busy());
        QCOMPARE(points.count(), 1);
        QCOMPARE(inputReports.count(), 2);
        QCOMPARE(reports.count(), 0);
        scripted->deliver(Command::CalibrationReport,
                          LegacyCanFdCodec::u16(6) + QByteArray(6, 0));
        QVERIFY(!d.busy());
        QCOMPARE(reports.last().at(0).toUInt(), 0u);
        QCOMPARE(reports.last().at(1).toUInt(), 0u);
        QCOMPARE(reports.last().at(2).toUInt(), 0u);
        QCOMPARE(messages.last().at(0).toString(), QString("输出轴校准成功 · step 6"));

        d.calibrate(true);
        scripted->deliver(Command::CalibrateOutput, LegacyCanFdCodec::u16(0));
        QVERIFY(d.busy());
        QCOMPARE(commandResponses.count(), 2);
        scripted->deliver(Command::CalibrationReport,
                          LegacyCanFdCodec::u16(6) + LegacyCanFdCodec::u16(0) +
                              LegacyCanFdCodec::u16(2) + LegacyCanFdCodec::u16(7));
        QVERIFY(!d.busy());
        QCOMPARE(messages.last().at(0).toString(),
                 QString("输出轴校准未成功 · step 6 · DATA0=0 · DATA1=2 · DATA2=7"));
        QCOMPARE(reports.last().at(0).toUInt(), 0u);
        QCOMPARE(reports.last().at(1).toUInt(), 2u);
        QCOMPARE(reports.last().at(2).toUInt(), 7u);
    }
    void calibrationControlsCanReplaceAStaleLocalWait() {
        auto transport = std::make_unique<ScriptedTransport>();
        auto* scripted = transport.get();
        DeviceService d(std::move(transport), nullptr);
        d.connectDevice({});
        QVERIFY(d.connected());

        d.calibrate(false);
        QVERIFY(d.busy());
        QCOMPARE(scripted->sent().last().command, code(Command::CalibrateInput));

        d.calibrate(true);
        QVERIFY(d.busy());
        QCOMPARE(scripted->sent().last().command, code(Command::CalibrateOutput));
        scripted->deliver(Command::CalibrationReport,
                          LegacyCanFdCodec::u16(5) + QByteArray(6, 0));
        QVERIFY(d.busy());
        scripted->deliver(Command::CalibrationReport,
                          LegacyCanFdCodec::u16(6) + QByteArray(6, 0));
        QVERIFY(!d.busy());
    }
    void recording() {
        RecordingService defaultRecorder;
        QCOMPARE(defaultRecorder.directory(),
                 QDir::cleanPath(QDir(QCoreApplication::applicationDirPath()).filePath("records")));
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        RecordingService r(dir.path());
        QSignalSpy directoryChanged(&r, &RecordingService::directoryChanged);
        const auto alternate = dir.filePath("selected-records");
        QVERIFY(r.setDirectory(alternate));
        QCOMPARE(r.directory(), QDir::cleanPath(alternate));
        QCOMPARE(directoryChanged.count(), 1);
        QVERIFY(r.start(false));
        QVERIFY(!r.hasAutomaticCloseDeadline());
        QVERIFY(!r.setDirectory(dir.filePath("blocked-while-recording")));
        Telemetry t;
        t.seconds = 1;
        t.position = 2;
        r.append(t, {});
        r.stop();
        auto files = QDir(alternate).entryList({"motion*.csv"}, QDir::Files);
        QCOMPARE(files.size(), 1);
        QFile file(QDir(alternate).filePath(files.first()));
        QVERIFY(file.open(QIODevice::ReadOnly));
        auto bytes = file.readAll();
        QVERIFY(bytes.startsWith("time_s,position_real,position_target,velocity_real,velocity_target,"
                                 "torque_real,torque_target\n"));
        QVERIFY(!bytes.contains("SIMULATED"));
        QVERIFY(bytes.contains("1.000000,2.00000,0.00000,0.00000,0.00000,0.00000,0.00000\n"));
        QVERIFY(!bytes.contains(",,"));
        QVERIFY(r.start(true));
        QVERIFY(r.hasAutomaticCloseDeadline());
        r.stop();
        QCOMPARE(RecordingService::csvCell("x,\"y"), QByteArray("\"x,\"\"y\""));
        QCOMPARE(RecordingService::csvCell("=1+1"), QByteArray("\"'=1+1\""));
        TestResult first{{0.1f, -0.2f, 0.3f, 0.5f, 0.01f, -0.4f, 0.6f, 1.0f}, "测试成功"};
        QVERIFY(r.saveTest({"03", "01", "SN001"}, {}, first));
        QVERIFY(r.saveTest({"99", "88", "SN002"}, {}, first));
        const auto inspectionName = QString("0301_%1.csv").arg(QDate::currentDate().toString("yyyyMMdd"));
        QFile inspectionFile(QDir(alternate).filePath(inspectionName));
        QVERIFY(inspectionFile.open(QIODevice::ReadOnly));
        const auto inspectionLines = inspectionFile.readAll().split('\n');
        QCOMPARE(inspectionLines.at(0),
                 QByteArray("Index,DeviceType,DeviceID,SN,Timestamp,Linear Error,Speed Torque Min,"
                            "Speed Torque Max,Speed Torque Range,Angle Error Max,Position Torque Min,"
                            "Position Torque Max,Position Torque Range,Result"));
        QVERIFY(inspectionLines.at(1).startsWith("1,03,01,SN001,"));
        QVERIFY(inspectionLines.at(1).endsWith(",0.10000,-0.20000,0.30000,0.50000,0.01000,-0.40000,"
                                               "0.60000,1.00000,测试成功"));
        QVERIFY(inspectionLines.at(2).startsWith("2,99,88,SN002,"));
    }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "CoreTests.moc"
