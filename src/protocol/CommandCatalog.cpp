#include "CommandCatalog.h"
#include <array>

namespace studio {
namespace {
using E = ProtocolEvidence;
using O = std::optional<quint8>;

constexpr std::array kSpecs{
    CommandSpec{Command::Disable, "CAN_CMD_MOTOR_DISABLE", O{0}, O{1}, 1, E::V3AndLegacyAgree, "0 means success"},
    CommandSpec{Command::Enable, "CAN_CMD_MOTOR_ENABLE", O{0}, O{2}, 2, E::V3AndLegacyAgree, "uint16 error word"},
    CommandSpec{Command::CalibrateInput, "CAN_CMD_MOTOR_CALIBRATION", O{0}, O{2}, 2, E::V3AndLegacyAgree, "ACK starts calibration; command 31 reports progress"},
    CommandSpec{Command::Motion, "CAN_CMD_MOTOR_CONTROL", O{10}, O{10}, 16, E::V3AndLegacyAgree, "3 float + 2 uint16; serialized in the legacy Windows program's little-endian byte order"},
    CommandSpec{Command::Reset, "CAN_CMD_MOTOR_RESET", O{0}, O{1}, 1, E::V3AndLegacyAgree, "0 means success"},
    CommandSpec{Command::SetHome, "CAN_CMD_SET_HOME", O{0}, O{1}, 1, E::V3AndLegacyAgree, "0 success; 1 invalid running/state condition; 2 repeated within 0.5 s"},
    CommandSpec{Command::SetGear, "CAN_CMD_SET_GEAR", O{4}, O{4}, 4, E::V3AndLegacyAgree, "float gear ratio"},
    CommandSpec{Command::SetId, "CAN_CMD_SET_ID", O{1}, O{1}, 1, E::V3AndLegacyAgree, "uint8 motor ID"},
    CommandSpec{Command::SetTimeout, "CAN_CMD_SET_CAN_TIMEOUT", O{1}, O{1}, 1, E::V3AndLegacyAgree, "uint8 timeout in ms"},
    CommandSpec{Command::SetBandwidth, "CAN_CMD_SET_BANDWIDTH", O{2}, O{2}, 2, E::V3AndLegacyAgree, "uint16 current-loop bandwidth in Hz"},
    CommandSpec{Command::SetCurrent, "CAN_CMD_SET_LIMIT_CURRENT", O{2}, O{2}, 2, E::V3AndLegacyAgree, "uint16 limit value x10"},
    CommandSpec{Command::SetUnderVoltage, "CAN_CMD_SET_UNDER_VOLTAGE", O{2}, O{2}, 2, E::V3AndLegacyAgree, "uint16 voltage x10"},
    CommandSpec{Command::SetOverVoltage, "CAN_CMD_SET_OVER_VOLTAGE", O{2}, O{2}, 2, E::V3AndLegacyAgree, "uint16 voltage x10"},
    CommandSpec{Command::SetMotorTemperature, "CAN_CMD_SET_MOTOR_TEMPERATURE", O{2}, O{2}, 2, E::V3AndLegacyAgree, "uint16 temperature x10"},
    CommandSpec{Command::SetDriverTemperature, "CAN_CMD_SET_DRIVE_TEMPERATURE", O{2}, O{1}, 2, E::ConflictingSources, "V3 table declares response DLC 1 but also declares a uint16 payload", O{2}},
    CommandSpec{Command::SaveConfig, "CAN_CMD_SAVE_CONFIG", O{0}, O{1}, 1, E::V3AndLegacyAgree, "0 success; 1 failure"},
    CommandSpec{Command::ClearError, "CAN_CMD_ERROR_RESET", O{0}, O{1}, 1, E::V3AndLegacyAgree, "0 means success"},
    CommandSpec{Command::FirmwareStart, "CAN_CMD_WRITE_APP_BACK_START", O{0}, O{1}, 1, E::V3AndLegacyAgree, "0 erase success; 1 failure"},
    CommandSpec{Command::FirmwareWrite, "CAN_CMD_WRITE_APP_BACK", O{8}, O{1}, 1, E::V3AndLegacyAgree, "8-byte firmware block; legacy flow does not await each ACK"},
    CommandSpec{Command::FirmwareCheck, "CAN_CMD_CHECK_APP_BACK", O{8}, O{1}, 1, E::ConflictingSources, "V3 response DLC 1; deployed legacy constant says 2 but runtime only reads byte0", O{2}},
    CommandSpec{Command::FirmwareBoot, "CAN_CMD_DFU_START", O{0}, std::nullopt, 1, E::ConflictingSources, "legacy response DLC is not declared although parser reads byte0"},
    CommandSpec{Command::Version, "CAN_CMD_GET_FW_VERSION", O{0}, O{4}, 4, E::ConflictingSources, "V3 response DLC 4; deployed legacy constant says 2 while runtime reads four bytes", O{2}},
    CommandSpec{Command::Status, "CAN_CMD_GET_STATUSWORD", O{0}, O{2}, 2, E::V3AndLegacyAgree, "uint16 status word; see StatusWordCatalog"},
    CommandSpec{Command::Config, "CAN_CMD_GET_CONFIG", O{0}, O{11}, 18, E::V3AndLegacyAgree, "20-byte CAN FD payload; 18 defined bytes plus 2 reserved"},
    CommandSpec{Command::CalibrateOutput, "CAN_CMD_OUT_ENCODER_CALIBRATION", O{0}, O{2}, 2, E::V3AndLegacyAgree, "0 starts calibration; non-zero is an error code"},
    CommandSpec{Command::Encoder, "CAN_CMD_GET_OUT_ENCODER", O{0}, O{4}, 4, E::V3AndLegacyAgree, "float output encoder angle"},
    CommandSpec{Command::StopEncoder, "CAN_CMD_STOP_GET_OUT_ENCODER_ANGLE", O{0}, O{1}, 1, E::V3AndLegacyAgree, "0 means success"},
    CommandSpec{Command::GetCanId, "CAN_CMD_GET_CANID", O{0}, O{0}, 0, E::V3Only, "request uses the protocol's special target CAN ID 0x1f"},
    CommandSpec{Command::CalibrationReport, "CAN_CMD_CALIB_REPORT", std::nullopt, O{8}, 8, E::V3AndLegacyAgree, "receive-only staged calibration report; step 6 completes output calibration"},
    CommandSpec{Command::TestScope, "CAN_CMD_SET_TEST_SCOPE", O{10}, O{1}, 1, E::LegacySourceOnly, "4 float thresholds; response 1 means accepted"},
    CommandSpec{Command::TestStart, "CAN_CMD_SET_START_TEST", O{0}, O{1}, 1, E::LegacySourceOnly, "response 1 means accepted"},
    CommandSpec{Command::TestStep, "CAN_CMD_GET_TEST_STEP", O{0}, O{1}, 1, E::LegacySourceOnly, "uint8 step 1..7"},
    CommandSpec{Command::TestData, "CAN_CMD_GET_TEST_DATA", O{0}, O{13}, 32, E::LegacySourceOnly, "8 float result values"},
    CommandSpec{Command::SetCanFdDataRate, "CAN_CMD_SET_CANFD_DATA_RATE", O{1}, O{1}, 1, E::V3Only, "uint8 data bitrate in Mbps; only 5 or 8"},
    CommandSpec{Command::GetPhaseAngle, "CAN_CMD_GET_PHASE_ANGLE", O{0}, O{8}, 8, E::V3Only, "int32 UVW phase order and float electrical-angle offset"},
    CommandSpec{Command::GetAngleError, "CAN_CMD_GET_ANGLE_ERROR", O{0}, O{10}, 16, E::V3Only, "reference angle, joint angle, angle error in radians and degrees"},
    CommandSpec{Command::GetCalibration, "CAN_CMD_GET_Calibration", O{0}, O{8}, 8, E::V3Only, "int32 motor phase order and float electrical angle"},
    CommandSpec{Command::GetJointStatus, "CAN_CMD_GET_JOINTSTATUS", O{0}, O{8}, 8, E::V3Only, "uint16 joint state, uint16 fault code and float bus voltage"},
    CommandSpec{Command::MotionCrAsu, "CAN_CMD_MOTOR_CONTROL_CR_ASU", O{10}, O{11}, 20, E::V3Only, "motion feedback plus bus voltage and fault code"},
    CommandSpec{Command::GetCanFdTxRxCount, "CAN_CMD_GET_CANFD_TX_RX_COUNT", O{0}, O{8}, 8, E::V3Only, "uint32 RX count and uint32 TX count"},
};
} // 匿名命名空间

std::span<const CommandSpec> commandSpecs() { return kSpecs; }

const CommandSpec* commandSpec(quint8 value) {
    for (const auto& spec : kSpecs)
        if (code(spec.command) == value)
            return &spec;
    return nullptr;
}

int canFdLength(quint8 dlc) {
    constexpr int lengths[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 16, 20, 24, 32, 48, 64};
    return dlc < 16 ? lengths[dlc] : -1;
}

int canFdDlcForLength(int length) {
    for (int dlc = 0; dlc < 16; ++dlc)
        if (canFdLength(quint8(dlc)) >= length)
            return dlc;
    return -1;
}

bool validRequestPayload(quint8 command, int payloadBytes) {
    const auto* spec = commandSpec(command);
    return spec && spec->requestDlc && canFdLength(*spec->requestDlc) == payloadBytes;
}

bool validResponsePayload(quint8 command, int payloadBytes) {
    const auto* spec = commandSpec(command);
    if (!spec)
        return true; // 保留未知帧供诊断使用。
    if (spec->evidence == ProtocolEvidence::ConflictingSources) {
        int minimum = spec->responseSemanticBytes;
        if (spec->responseDlc)
            minimum = qMin(minimum, canFdLength(*spec->responseDlc));
        if (spec->compatibleResponseDlc)
            minimum = qMin(minimum, canFdLength(*spec->compatibleResponseDlc));
        return payloadBytes >= minimum;
    }
    if (!spec->responseDlc)
        return payloadBytes >= spec->responseSemanticBytes;
    // 老接收器读取已知字段，并忽略 CAN-FD 填充或后续扩展字节。
    // 保持该行为，但仍拒绝长度不足以供命令解析器读取的帧。
    return payloadBytes >= spec->responseSemanticBytes;
}

} // 命名空间 studio
