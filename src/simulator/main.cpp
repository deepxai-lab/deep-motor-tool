#include "DeviceSimulatorServer.h"
#include <QCoreApplication>
#include <QTextStream>

using namespace studio;

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("DEEP Device Simulator");
    const auto args = app.arguments();
    QHostAddress bindAddress = QHostAddress::LocalHost;
    const int bindIndex = args.indexOf("--bind");
    if (bindIndex >= 0 && bindIndex + 1 < args.size())
        bindAddress = QHostAddress(args[bindIndex + 1]);

    DeviceSimulatorServer server;
    QTextStream output(stdout);
    quint64 motionFrames = 0;
    QObject::connect(&server, &DeviceSimulatorServer::configured, &app,
                     [&output](quint8 board, quint32 bitrate) {
                         output << "CONFIG board=" << int(board) << " data_bitrate=" << bitrate << '\n';
                         output.flush();
                     });
    QObject::connect(&server, &DeviceSimulatorServer::requestReceived, &app,
                     [&output, &motionFrames](const CanFrame& frame) {
                         if (frame.command == code(Command::Motion) && (++motionFrames % 250) != 0)
                             return;
                         output << "RX board=" << int(frame.board) << " can=" << int(frame.channel)
                                << " motor=" << int(frame.motor) << " command=" << int(frame.command)
                                << " bytes=" << frame.payload.size();
                         if (frame.command == code(Command::Motion))
                             output << " total_motion_frames=" << motionFrames;
                         output << '\n';
                         output.flush();
                     });
    QObject::connect(&server, &DeviceSimulatorServer::protocolError, &app,
                     [&output](const QString& error) {
                         output << "ERROR " << error << '\n';
                         output.flush();
                     });
    if (bindAddress.isNull() || !server.listen(bindAddress, 43893))
        return 2;
    output << "DEEP EtherCAN/CAN FD simulator listening on " << bindAddress.toString() << ":43893\n";
    output.flush();
    return app.exec();
}
