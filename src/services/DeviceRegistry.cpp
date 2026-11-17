#include "DeviceRegistry.h"
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>

namespace studio {
DeviceRegistry::DeviceRegistry(QString file, QObject* parent) : QObject(parent) {
    settings_ = file.isEmpty() ? std::make_unique<QSettings>()
                               : std::make_unique<QSettings>(file, QSettings::IniFormat);
    load();
}
void DeviceRegistry::load() {
    if (!settings_->contains("registry/schema")) {
        boards_.append(BoardProfile{});
        motors_.append(DeviceAddress{});
        return;
    }
    const auto data = QJsonDocument::fromJson(settings_->value("registry/data").toByteArray()).object();
    for (const auto& value : data["boards"].toArray()) {
        auto o = value.toObject();
        QHostAddress ip(o["ip"].toString());
        int rate = o["bitrate"].toInt();
        if (ip.protocol() != QAbstractSocket::IPv4Protocol || (rate != 5000000 && rate != 8000000))
            continue;
        if (std::any_of(boards_.begin(), boards_.end(), [&](const auto& b) { return b.ip == ip.toString(); }))
            continue;
        boards_.append({ip.toString(), quint32(rate), "deep.ethercan.canfd.v1"});
    }
    for (const auto& value : data["motors"].toArray()) {
        auto o = value.toObject();
        int ch = o["channel"].toInt(-1), id = o["id"].toInt(-1);
        QString ip = o["ip"].toString();
        if (ch < 0 || ch > 3 || id < 0 || id > 15 ||
            !std::any_of(boards_.begin(), boards_.end(), [&](const auto& b) { return b.ip == ip; }))
            continue;
        DeviceAddress a{ip, quint8(ch), quint8(id)};
        if (!motors_.contains(a))
            motors_.append(a);
    }
}
void DeviceRegistry::save() {
    QJsonArray boards, motors;
    for (const auto& b : boards_)
        boards.append(QJsonObject{{"ip", b.ip}, {"bitrate", int(b.dataBitrate)}, {"protocol", b.protocolId}});
    for (const auto& m : motors_)
        motors.append(QJsonObject{{"ip", m.ip}, {"channel", m.channel}, {"id", m.motor}});
    settings_->setValue("registry/schema", 1);
    settings_->setValue(
        "registry/data",
        QJsonDocument(QJsonObject{{"boards", boards}, {"motors", motors}}).toJson(QJsonDocument::Compact));
    settings_->sync();
    if (settings_->status() != QSettings::NoError)
        emit errorOccurred("设备配置保存失败");
    emit changed();
}
bool DeviceRegistry::addBoard(BoardProfile b) {
    QHostAddress ip(b.ip.trimmed());
    if (ip.protocol() != QAbstractSocket::IPv4Protocol ||
        (b.dataBitrate != 5000000 && b.dataBitrate != 8000000) || b.protocolId != "deep.ethercan.canfd.v1") {
        emit errorOccurred("通信板地址、波特率或协议无效");
        return false;
    }
    b.ip = ip.toString();
    if (std::any_of(boards_.begin(), boards_.end(), [&](const auto& v) { return v.ip == b.ip; })) {
        emit errorOccurred("该 IP 的通信板已存在");
        return false;
    }
    boards_.append(b);
    save();
    return true;
}
bool DeviceRegistry::removeBoard(const QString& ip) {
    auto count = boards_.size();
    boards_.removeIf([&](const auto& b) { return b.ip == ip; });
    if (count == boards_.size())
        return false;
    motors_.removeIf([&](const auto& m) { return m.ip == ip; });
    save();
    return true;
}
bool DeviceRegistry::addMotor(DeviceAddress a) {
    if (a.channel > 3 || a.motor > 15 ||
        !std::any_of(boards_.begin(), boards_.end(), [&](const auto& b) { return b.ip == a.ip; })) {
        emit errorOccurred("请先添加对应通信板，并设置有效的通道和电机 ID");
        return false;
    }
    if (motors_.contains(a)) {
        emit errorOccurred("该通道与电机 ID 已存在");
        return false;
    }
    motors_.append(a);
    save();
    return true;
}
bool DeviceRegistry::removeMotor(int i) {
    if (i < 0 || i >= motors_.size())
        return false;
    motors_.removeAt(i);
    save();
    return true;
}

bool DeviceRegistry::storeDiscoveredSession(const DeviceSession& session) {
    QHostAddress ip(session.address.ip.trimmed());
    if (ip.protocol() != QAbstractSocket::IPv4Protocol || session.address.channel > 3 ||
        session.address.motor > 15 ||
        (session.dataBitrate != 5000000 && session.dataBitrate != 8000000) ||
        session.protocolId != "deep.ethercan.canfd.v1") {
        emit errorOccurred("自动连接结果无效，未保存设备");
        return false;
    }
    const QString normalizedIp = ip.toString();
    bool changed = false;
    auto board = std::find_if(boards_.begin(), boards_.end(),
                              [&](const auto& item) { return item.ip == normalizedIp; });
    if (board == boards_.end()) {
        boards_.append({normalizedIp, session.dataBitrate, session.protocolId});
        changed = true;
    } else if (board->dataBitrate != session.dataBitrate || board->protocolId != session.protocolId) {
        board->dataBitrate = session.dataBitrate;
        board->protocolId = session.protocolId;
        changed = true;
    }
    const DeviceAddress address{normalizedIp, session.address.channel, session.address.motor};
    if (!motors_.contains(address)) {
        motors_.append(address);
        changed = true;
    }
    if (changed)
        save();
    return true;
}
} // 命名空间 studio
