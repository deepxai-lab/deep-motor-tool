#include "FrameDiagnostics.h"
#include "protocol/LegacyCanFdCodec.h"
#include "protocol/StatusWordCatalog.h"

namespace studio {
namespace {
QString resolved(const FrameDiagnostics::TextResolver& resolve, const QString& source) {
    return resolve ? resolve(source) : source;
}

QString statusLabel(MotorStatusFlag flag) {
    switch (flag) {
    case MotorStatusFlag::OverVoltageProtection: return QStringLiteral("过压保护");
    case MotorStatusFlag::UnderVoltageProtection: return QStringLiteral("欠压保护");
    case MotorStatusFlag::OverCurrentProtection: return QStringLiteral("过流保护");
    case MotorStatusFlag::MotorOverTemperatureProtection: return QStringLiteral("电机过温保护");
    case MotorStatusFlag::DriverOverTemperatureProtection: return QStringLiteral("驱动器过温保护");
    case MotorStatusFlag::CanTimeout: return QStringLiteral("CAN 超时");
    case MotorStatusFlag::MotorTemperatureSensorLost: return QStringLiteral("电机温度线丢失");
    case MotorStatusFlag::HostCompatibilityMarker: return QStringLiteral("关节位置校验异常");
    case MotorStatusFlag::OutputEncoderCalibrationFailed: return QStringLiteral("输出端编码器校准失败");
    case MotorStatusFlag::OutputEncoderLost: return QStringLiteral("输出端编码器丢失");
    case MotorStatusFlag::InputEncoderLost: return QStringLiteral("输入端编码器丢失");
    case MotorStatusFlag::InputEncoderCompensationAbnormal: return QStringLiteral("输入端编码器补偿异常");
    case MotorStatusFlag::CurrentSamplingBiasAbnormal: return QStringLiteral("电流采样偏置异常");
    case MotorStatusFlag::UvwPhaseOverCurrent: return QStringLiteral("UVW 相线过流");
    case MotorStatusFlag::OutputEncoderUncalibrated: return QStringLiteral("输出端编码器未校准");
    case MotorStatusFlag::TwoInOneFirmwareMissing: return QStringLiteral("未刷 2in1 固件");
    }
    return {};
}
}

QStringList FrameDiagnostics::statusWordFaults(quint16 word, bool includeCodes,
                                               const TextResolver& resolve) {
    QStringList faults;
    for (const auto& spec : statusBitSpecs()) {
        if (!hasStatusFlag(word, spec.flag))
            continue;
        auto label = resolved(resolve, statusLabel(spec.flag));
        if (includeCodes) {
            const auto mask = quint16(1u << static_cast<quint8>(spec.flag));
            label += QStringLiteral(" (0x") +
                     QStringLiteral("%1").arg(mask, 4, 16, QLatin1Char('0')).toUpper() +
                     QStringLiteral(")");
        }
        faults << label;
    }
    return faults;
}

QString FrameDiagnostics::commandName(quint8 value, const TextResolver& resolve) {
    QString source;
    switch (static_cast<Command>(value)) {
    case Command::Disable: source = "失能"; break;
    case Command::Enable: source = "使能"; break;
    case Command::CalibrateInput: source = "输入轴校准"; break;
    case Command::Motion: source = "电机控制/状态"; break;
    case Command::Reset: source = "复位"; break;
    case Command::SetHome: source = "设置零点"; break;
    case Command::SetGear: source = "设置减速比"; break;
    case Command::SetId: source = "设置电机 ID"; break;
    case Command::SetTimeout: source = "设置超时"; break;
    case Command::SetBandwidth: source = "设置带宽"; break;
    case Command::SetCurrent: source = "设置电流"; break;
    case Command::SetUnderVoltage: source = "设置欠压"; break;
    case Command::SetOverVoltage: source = "设置过压"; break;
    case Command::SetMotorTemperature: source = "设置电机温度阈值"; break;
    case Command::SetDriverTemperature: source = "设置驱动器温度阈值"; break;
    case Command::SaveConfig: source = "保存配置"; break;
    case Command::ClearError: source = "清除错误"; break;
    case Command::FirmwareStart: source = "固件升级开始"; break;
    case Command::FirmwareWrite: source = "固件数据"; break;
    case Command::FirmwareCheck: source = "固件校验"; break;
    case Command::FirmwareBoot: source = "固件启动"; break;
    case Command::Version: source = "版本"; break;
    case Command::Status: source = "状态字"; break;
    case Command::Config: source = "参数配置"; break;
    case Command::CalibrateOutput: source = "输出轴校准"; break;
    case Command::Encoder: source = "编码器数据"; break;
    case Command::StopEncoder: source = "停止编码器采集"; break;
    case Command::GetCanId: source = "查询 CAN ID"; break;
    case Command::CalibrationReport: source = "校准过程数据"; break;
    case Command::TestScope: source = "测试阈值"; break;
    case Command::TestStart: source = "开始测试"; break;
    case Command::TestStep: source = "测试步骤"; break;
    case Command::TestData: source = "测试结果"; break;
    case Command::SetCanFdDataRate: source = "设置CAN FD数据波特率"; break;
    case Command::GetPhaseAngle: source = "UVW 相位与电角度"; break;
    case Command::GetAngleError: source = "角度误差"; break;
    case Command::GetCalibration: source = "读取校准信息"; break;
    case Command::GetJointStatus: source = "读取关节状态"; break;
    case Command::MotionCrAsu: source = "扩展电机控制/状态"; break;
    case Command::GetCanFdTxRxCount: source = "CAN FD 收发计数"; break;
    default: source = "未知命令"; break;
    }
    return resolved(resolve, source);
}

QString FrameDiagnostics::summary(const CanFrame& frame, const TextResolver& resolve) {
    using Codec = LegacyCanFdCodec;
    const auto& p = frame.payload;
    switch (static_cast<Command>(frame.command)) {
    case Command::SetCanFdDataRate:
        if (!p.isEmpty())
            return resolved(resolve, "CAN FD数据波特率 %1 Mbps").arg(quint8(p[0]));
        break;
    case Command::Motion:
        if (const auto telemetry = Codec::telemetry(p))
            return resolved(resolve, "角度 %1 rad；角速度 %2 rad/s；扭矩 %3 N·m；电机 %4 °C；驱动器 %5 °C")
                .arg(telemetry->position, 0, 'f', 4).arg(telemetry->velocity, 0, 'f', 4)
                .arg(telemetry->torque, 0, 'f', 4).arg(telemetry->motorTemperature, 0, 'f', 2)
                .arg(telemetry->driverTemperature, 0, 'f', 2);
        break;
    case Command::Version:
        if (p.size() >= 4)
            return resolved(resolve, "版本 V%1-%2").arg(Codec::readU16(p, 0)).arg(Codec::readU16(p, 2));
        break;
    case Command::Status:
        if (p.size() >= 2) {
            const auto word = Codec::readU16(p, 0);
            if (word == 0)
                return resolved(resolve, "状态正常");
            const auto faults = statusWordFaults(word, false, resolve);
            return resolved(resolve, "状态字 0x%1：")
                       .arg(word, 4, 16, QLatin1Char('0')) + faults.join(resolved(resolve, "，"));
        }
        break;
    case Command::Encoder:
        if (p.size() >= 4)
            return resolved(resolve, "编码器值 %1").arg(Codec::readFloat(p, 0), 0, 'f', 6);
        break;
    case Command::CalibrationReport:
        if (p.size() >= 2) {
            const auto step = Codec::readU16(p, 0);
            if (step == 2 && p.size() >= 8)
                return resolved(resolve, "阶段 2；电机编码器校准值 %1").arg(Codec::readFloat(p, 4), 0, 'f', 6);
            if (step == 3 && p.size() >= 8) {
                const auto raw = qint32(quint32(Codec::readU16(p, 4)) | (quint32(Codec::readU16(p, 6)) << 16));
                return resolved(resolve, "阶段 3；齿槽补偿值 %1").arg(raw);
            }
            if (step == 6 && p.size() >= 8)
                return resolved(resolve, "阶段 6；输出轴结果 DATA0=%1，DATA1=%2，DATA2=%3")
                    .arg(Codec::readU16(p, 2))
                    .arg(Codec::readU16(p, 4))
                    .arg(Codec::readU16(p, 6));
            return resolved(resolve, "校准阶段 %1").arg(step);
        }
        break;
    case Command::TestStep:
        if (!p.isEmpty())
            return resolved(resolve, "测试步骤 %1").arg(quint8(p[0]));
        break;
    case Command::TestData:
        if (p.size() >= 32) {
            QStringList values;
            for (int i = 0; i < 8; ++i)
                values << QString::number(Codec::readFloat(p, i * 4), 'f', 5);
            return resolved(resolve, "8 项测试结果：") + values.join(resolved(resolve, "，"));
        }
        break;
    case Command::GetPhaseAngle:
        if (p.size() >= 8) {
            const qint32 phase = Codec::readI32(p, 0);
            const QString order = phase == 0 ? resolved(resolve, "UVW正向")
                                  : phase == 1 ? resolved(resolve, "UWV反向")
                                               : resolved(resolve, "未知相序");
            return resolved(resolve, "相序 %1（%2）；电气角度偏置 %3 rad")
                .arg(phase)
                .arg(order)
                .arg(Codec::readFloat(p, 4), 0, 'f', 6);
        }
        break;
    case Command::GetAngleError:
        if (p.size() >= 16)
            return resolved(resolve,
                            "参考角度 %1 rad；关节端角度 %2 rad；角度误差 %3 rad / %4°")
                .arg(Codec::readFloat(p, 0), 0, 'f', 6)
                .arg(Codec::readFloat(p, 4), 0, 'f', 6)
                .arg(Codec::readFloat(p, 8), 0, 'f', 6)
                .arg(Codec::readFloat(p, 12), 0, 'f', 4);
        break;
    case Command::Config:
        if (p.size() >= 18)
            return resolved(resolve, "减速比 %1；电机 ID %2；超时 %3；带宽 %4 Hz")
                .arg(Codec::readFloat(p, 0), 0, 'f', 3).arg(quint8(p[4])).arg(quint8(p[5]))
                .arg(Codec::readU16(p, 6));
        break;
    case Command::GetCalibration:
        if (p.size() >= 8) {
            const auto phase = qint32(quint32(Codec::readU16(p, 0)) |
                                      (quint32(Codec::readU16(p, 2)) << 16));
            return resolved(resolve, "电机相序 %1；电气角度 %2 rad")
                .arg(phase)
                .arg(Codec::readFloat(p, 4), 0, 'f', 6);
        }
        break;
    case Command::GetJointStatus:
        if (p.size() >= 8)
            return resolved(resolve, "关节状态 %1；故障码 0x%2；母线电压 %3 V")
                .arg(Codec::readU16(p, 0))
                .arg(Codec::readU16(p, 2), 4, 16, QLatin1Char('0'))
                .arg(Codec::readFloat(p, 4), 0, 'f', 2);
        break;
    case Command::MotionCrAsu:
        if (p.size() >= 20) {
            const auto telemetry = Codec::telemetry(p);
            if (telemetry)
                return resolved(resolve, "角度 %1 rad；角速度 %2 rad/s；扭矩 %3 N·m；母线电压 %4 V；故障码 0x%5")
                    .arg(telemetry->position, 0, 'f', 4)
                    .arg(telemetry->velocity, 0, 'f', 4)
                    .arg(telemetry->torque, 0, 'f', 4)
                    .arg(qint16(Codec::readU16(p, 16)) / 100.0, 0, 'f', 2)
                    .arg(Codec::readU16(p, 18), 4, 16, QLatin1Char('0'));
        }
        break;
    case Command::GetCanFdTxRxCount:
        if (p.size() >= 8) {
            const auto readU32 = [&p](int offset) {
                return quint32(Codec::readU16(p, offset)) |
                       (quint32(Codec::readU16(p, offset + 2)) << 16);
            };
            return resolved(resolve, "CAN FD 接收 %1 帧；发送 %2 帧")
                .arg(readU32(0))
                .arg(readU32(4));
        }
        break;
    case Command::Enable:
    case Command::CalibrateInput:
    case Command::CalibrateOutput:
        if (p.size() >= 2)
            return resolved(resolve, "应答码 %1").arg(Codec::readU16(p, 0));
        break;
    case Command::TestScope:
    case Command::TestStart:
        if (!p.isEmpty())
            return resolved(resolve, "应答值 %1").arg(quint8(p[0]));
        break;
    default:
        if (p.size() == 1)
            return resolved(resolve, "应答码 %1").arg(quint8(p[0]));
        if (p.size() == 2)
            return resolved(resolve, "应答值 %1").arg(Codec::readU16(p, 0));
        break;
    }
    return resolved(resolve, p.isEmpty() ? "无数据区" : "原始数据");
}
} // 命名空间 studio
