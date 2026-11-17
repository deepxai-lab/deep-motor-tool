#include "RecordingService.h"
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>

namespace studio {
namespace {
constexpr auto InspectionHeader =
    "Index,DeviceType,DeviceID,SN,Timestamp,Linear Error,Speed Torque Min,Speed Torque Max,"
    "Speed Torque Range,Angle Error Max,Position Torque Min,Position Torque Max,"
    "Position Torque Range,Result";

bool isLegacyInspectionFile(const QString& path) {
    QFile input(path);
    return input.open(QIODevice::ReadOnly | QIODevice::Text) &&
           input.readLine().trimmed() == QByteArray(InspectionHeader);
}

int inspectionRowCount(const QString& path) {
    QFile input(path);
    if (!input.open(QIODevice::ReadOnly | QIODevice::Text))
        return 0;
    input.readLine();
    int rows = 0;
    while (!input.atEnd()) {
        if (!input.readLine().trimmed().isEmpty())
            ++rows;
    }
    return rows;
}

QByteArray inspectionCell(QString value) {
    if (!value.isEmpty() && !QString("=+-@\t\r").contains(value.front()) &&
        !value.contains(',') && !value.contains('"') && !value.contains('\n'))
        return value.toUtf8();
    return RecordingService::csvCell(std::move(value));
}
}

RecordingService::RecordingService(QString dir, QObject* parent)
    : QObject(parent), directory_(std::move(dir)) {
    if (directory_.isEmpty())
        directory_ = QDir(QCoreApplication::applicationDirPath()).filePath("records");
    directory_ = QDir::cleanPath(QFileInfo(directory_).absoluteFilePath());
    flushTimer_.setInterval(1000);
    connect(&flushTimer_, &QTimer::timeout, this, [this] {
        if (file_.isOpen() && !file_.flush()) {
            emit errorOccurred(file_.errorString());
            stop();
        }
    });
    autoCloseTimer_.setSingleShot(true);
    connect(&autoCloseTimer_, &QTimer::timeout, this, &RecordingService::stop);
}
bool RecordingService::setDirectory(QString directory) {
    directory = directory.trimmed();
    if (directory.isEmpty()) {
        emit errorOccurred("保存目录不能为空");
        return false;
    }
    if (isActive()) {
        emit errorOccurred("请先停止记录，再修改保存目录");
        return false;
    }
    directory = QDir::cleanPath(QFileInfo(directory).absoluteFilePath());
    if (!QDir().mkpath(directory)) {
        emit errorOccurred("无法创建保存目录：" + directory);
        return false;
    }
    if (directory_ == directory)
        return true;
    directory_ = std::move(directory);
    emit directoryChanged(directory_);
    return true;
}
QByteArray RecordingService::csvCell(QString value) {
    // 防止操作员或扫码枪输入触发电子表格公式注入。
    if (!value.isEmpty() && QString("=+-@\t\r").contains(value.front()))
        value.prepend('\'');
    value.replace('"', "\"\"");
    return '"' + value.toUtf8() + '"';
}
bool RecordingService::start(bool temperatures) {
    stop();
    temperatureOnly_ = temperatures;
    if (!QDir().mkpath(directory_)) {
        emit errorOccurred("无法创建记录目录");
        return false;
    }
    auto name = QString("/%1_%2.csv")
                    .arg(temperatures ? "temperature" : "motion",
                         QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss_zzz"));
    file_.setFileName(directory_ + name);
    if (!file_.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        emit errorOccurred(file_.errorString());
        return false;
    }
    if (!write(temperatures ? "timestamp,elapsed_s,driver_C,motor_C\n"
                            : "time_s,position_real,position_target,velocity_real,velocity_target,"
                              "torque_real,torque_target\n"))
        return false;
    flushTimer_.start();
    // 老程序的 2.5 小时截止时间只属于温度记录。
    // 运动记录跟随真实运行，并在运行停止时关闭。
    if (temperatures)
        autoCloseTimer_.start(9000000);
    emit changed(true, file_.fileName());
    return true;
}
void RecordingService::stop() {
    flushTimer_.stop();
    autoCloseTimer_.stop();
    if (!file_.isOpen())
        return;
    const auto path = file_.fileName();
    if (!file_.flush())
        emit errorOccurred(file_.errorString());
    file_.close();
    emit changed(false, path);
}
bool RecordingService::write(const QByteArray& b) {
    if (file_.write(b) == b.size())
        return true;
    emit errorOccurred(file_.errorString());
    stop();
    return false;
}
void RecordingService::append(const Telemetry& t, const Motion& target) {
    if (!isActive())
        return;
    if (temperatureOnly_) {
        QByteArray row = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8() + ',' +
                         QByteArray::number(t.seconds, 'f', 6);
        row += ',' + QByteArray::number(t.driverTemperature, 'f', 2) + ',' +
               QByteArray::number(t.motorTemperature, 'f', 2);
        write(row + '\n');
        return;
    }
    appendMotionSample(t.seconds, t, target);
}
void RecordingService::appendMotionSample(double seconds, const Telemetry& feedback,
                                          const Motion& target) {
    if (!isActive() || temperatureOnly_)
        return;
    QByteArray row = QByteArray::number(seconds, 'f', 6);
    row += ',' + QByteArray::number(feedback.position, 'f', 5);
    row += ',' + QByteArray::number(target.position, 'f', 5);
    row += ',' + QByteArray::number(feedback.velocity, 'f', 5);
    row += ',' + QByteArray::number(target.velocity, 'f', 5);
    row += ',' + QByteArray::number(feedback.torque, 'f', 5);
    row += ',' + QByteArray::number(target.torque, 'f', 5) + '\n';
    write(row);
}
bool RecordingService::saveTest(const Identity& id, const DeviceAddress& address, const TestResult& result) {
    Q_UNUSED(address);
    if (!QDir().mkpath(directory_)) {
        emit errorOccurred("无法创建记录目录");
        return false;
    }
    const QString date = QDate::currentDate().toString("yyyyMMdd");
    QDir directory(directory_);
    QString path;
    // 已部署上位机每天保留一个下线检测文件。
    // 仅复用表头完全一致的 14 列文件，避免向早期预览版生成的文件中
    // 追加布局不兼容的数据行。
    const auto dailyFiles = directory.entryList({QString("*_%1.csv").arg(date)},
                                                QDir::Files, QDir::Name);
    for (const auto& name : dailyFiles) {
        const auto candidate = directory.filePath(name);
        if (isLegacyInspectionFile(candidate)) {
            path = candidate;
            break;
        }
    }
    if (path.isEmpty())
        path = directory.filePath(QString("%1%2_%3.csv").arg(id.type, id.deviceId, date));

    const int index = QFileInfo::exists(path) ? inspectionRowCount(path) + 1 : 1;
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Append)) {
        emit errorOccurred(out.errorString());
        return false;
    }
    QByteArray row;
    if (out.size() == 0)
        row = QByteArray(InspectionHeader) + '\n';
    row += QByteArray::number(index) + ',' + inspectionCell(id.type) + ',' + inspectionCell(id.deviceId) +
           ',' + inspectionCell(id.serial) + ',' +
           QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz").toUtf8();
    for (float v : result.values)
        row += ',' + QByteArray::number(v, 'f', 5);
    row += ',' + inspectionCell(result.result) + '\n';
    if (out.write(row) != row.size() || !out.flush()) {
        emit errorOccurred(out.errorString());
        return false;
    }
    return true;
}
} // 命名空间 studio
