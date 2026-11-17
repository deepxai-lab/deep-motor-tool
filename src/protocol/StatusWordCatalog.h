#pragma once

#include <QtGlobal>
#include <span>
#include <string_view>

namespace studio {

enum class MotorStatusFlag : quint8 {
    OverVoltageProtection = 0,
    UnderVoltageProtection = 1,
    OverCurrentProtection = 2,
    MotorOverTemperatureProtection = 3,
    DriverOverTemperatureProtection = 4,
    CanTimeout = 5,
    MotorTemperatureSensorLost = 6,
    HostCompatibilityMarker = 7,
    OutputEncoderCalibrationFailed = 8,
    OutputEncoderLost = 9,
    InputEncoderLost = 10,
    InputEncoderCompensationAbnormal = 11,
    CurrentSamplingBiasAbnormal = 12,
    UvwPhaseOverCurrent = 13,
    OutputEncoderUncalibrated = 14,
    TwoInOneFirmwareMissing = 15,
};

struct StatusBitSpec {
    MotorStatusFlag flag;
    std::string_view symbol;
    bool handledByHost;
};

std::span<const StatusBitSpec> statusBitSpecs();
bool hasStatusFlag(quint16 statusWord, MotorStatusFlag flag);

} // 命名空间 studio
