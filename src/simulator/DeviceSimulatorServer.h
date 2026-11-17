#pragma once

#include "domain/Types.h"
#include <QElapsedTimer>
#include <QHash>
#include <QHostAddress>
#include <QObject>
#include <QTimer>
#include <QUdpSocket>

namespace studio {

// 面向已部署 EtherCAN v1 + CAN FD 协议的套接字级模拟器。
// 特意复用正式编解码器，不再实现第二套线上格式。
class DeviceSimulatorServer final : public QObject {
    Q_OBJECT
  public:
    explicit DeviceSimulatorServer(QObject* parent = nullptr);
    bool listen(const QHostAddress& address = QHostAddress::LocalHost, quint16 port = 43893);
    void close();
    bool isListening() const { return socket_.state() == QAbstractSocket::BoundState; }
    quint16 port() const { return socket_.localPort(); }

  signals:
    void configured(quint8 board, quint32 dataBitrate);
    void requestReceived(studio::CanFrame frame);
    void responseSent(studio::CanFrame frame);
    void protocolError(QString reason);

  private:
    struct MotorState {
        bool enabled = false;
        Motion target;
        float position = 0;
        QByteArray config;
        int calibrationStep = -1;
        int outputCalibrationTicks = -1;
        int inspectionTicks = -1;
        QHostAddress peerAddress;
        quint16 peerPort = 0;
    };

    static quint32 stateKey(const CanFrame& frame);
    static QByteArray defaultConfig(quint8 motor);
    void processPendingDatagrams();
    void processRequest(const CanFrame& request, quint8 sequence,
                        const QHostAddress& peerAddress, quint16 peerPort);
    void sendResponse(const CanFrame& request, QByteArray payload, quint8 sequence,
                      const QHostAddress& peerAddress, quint16 peerPort);
    void advanceTasks();

    QUdpSocket socket_;
    QTimer taskTimer_;
    QElapsedTimer clock_;
    QHash<quint32, MotorState> motors_;
    quint8 asynchronousSequence_ = 0;
};

} // 命名空间 studio
