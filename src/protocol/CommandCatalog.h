#pragma once

#include "domain/Types.h"
#include <optional>
#include <span>
#include <string_view>

namespace studio {

enum class ProtocolEvidence {
    V3AndLegacyAgree,
    V3Only,
    LegacySourceOnly,
    ConflictingSources,
};

// 电机级 CAN FD 命令约定的唯一事实来源。
// DLC 是线上 CAN FD 编码；semanticBytes 是该命令实际读写的字节数。
// DLC 缺失表示现有资料没有定义该方向。
struct CommandSpec {
    Command command;
    std::string_view symbol;
    std::optional<quint8> requestDlc;
    std::optional<quint8> responseDlc;
    quint8 responseSemanticBytes;
    ProtocolEvidence evidence;
    std::string_view note;
    // 已部署老程序中的部分常量与其解析器及 V3 表格不一致。
    // 保留可观察的线上长度，同时禁止越界读取。
    std::optional<quint8> compatibleResponseDlc = std::nullopt;
};

std::span<const CommandSpec> commandSpecs();
const CommandSpec* commandSpec(quint8 command);
int canFdLength(quint8 dlc);
int canFdDlcForLength(int length);
bool validRequestPayload(quint8 command, int payloadBytes);
bool validResponsePayload(quint8 command, int payloadBytes);

} // 命名空间 studio
