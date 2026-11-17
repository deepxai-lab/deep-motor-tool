#include "StatusWordCatalog.h"
#include <array>

namespace studio {
namespace {
constexpr std::array kStatusBits{
    StatusBitSpec{MotorStatusFlag::OverVoltageProtection, "OVER_VOLTAGE_PROTECTION", true},
    StatusBitSpec{MotorStatusFlag::UnderVoltageProtection, "UNDER_VOLTAGE_PROTECTION", true},
    StatusBitSpec{MotorStatusFlag::OverCurrentProtection, "OVER_CURRENT_PROTECTION", true},
    StatusBitSpec{MotorStatusFlag::MotorOverTemperatureProtection, "MOTOR_OVER_TEMPERATURE", true},
    StatusBitSpec{MotorStatusFlag::DriverOverTemperatureProtection, "DRIVER_OVER_TEMPERATURE", true},
    StatusBitSpec{MotorStatusFlag::CanTimeout, "CAN_TIMEOUT", true},
    StatusBitSpec{MotorStatusFlag::MotorTemperatureSensorLost, "MOTOR_TEMPERATURE_SENSOR_LOST", true},
    StatusBitSpec{MotorStatusFlag::HostCompatibilityMarker, "HOST_COMPATIBILITY_MARKER", false},
    StatusBitSpec{MotorStatusFlag::OutputEncoderCalibrationFailed, "OUTPUT_ENCODER_CALIBRATION_FAILED", true},
    StatusBitSpec{MotorStatusFlag::OutputEncoderLost, "OUTPUT_ENCODER_LOST", true},
    StatusBitSpec{MotorStatusFlag::InputEncoderLost, "INPUT_ENCODER_LOST", true},
    StatusBitSpec{MotorStatusFlag::InputEncoderCompensationAbnormal, "INPUT_ENCODER_COMPENSATION_ABNORMAL", true},
    StatusBitSpec{MotorStatusFlag::CurrentSamplingBiasAbnormal, "CURRENT_SAMPLING_BIAS_ABNORMAL", true},
    StatusBitSpec{MotorStatusFlag::UvwPhaseOverCurrent, "UVW_PHASE_OVER_CURRENT", true},
    StatusBitSpec{MotorStatusFlag::OutputEncoderUncalibrated, "OUTPUT_ENCODER_UNCALIBRATED", true},
    StatusBitSpec{MotorStatusFlag::TwoInOneFirmwareMissing, "TWO_IN_ONE_FIRMWARE_MISSING", true},
};
} // 匿名命名空间

std::span<const StatusBitSpec> statusBitSpecs() { return kStatusBits; }

bool hasStatusFlag(quint16 statusWord, MotorStatusFlag flag) {
    return (statusWord & (quint16(1) << static_cast<quint8>(flag))) != 0;
}

} // 命名空间 studio
