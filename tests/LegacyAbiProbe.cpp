// 可选审计工具。请使用原始 MinGW 7.3 编译，以检查旧线上 ABI。
// 此文件有意不加入独立上位机的 CMake 目标。
#include "../../deep_motor_tool/src/NetCanBoard/types/DataStructures.h"
#include <cstddef>
#include <cstdio>
int main() {
    std::printf("DataHeader=%zu can_config_t=%zu CanfdConfig=%zu DownFrame=%zu UpFrame=%zu\n",
                sizeof(DataHeader), sizeof(can_config_t), sizeof(CanfdConfig), sizeof(DownFrame),
                sizeof(UpFrame));
    std::printf("config.enable_canfd=%zu config.enable_tdc=%zu frame.buf=%zu tx.data=%zu rx.data=%zu\n",
                offsetof(can_config_t, enable_canfd), offsetof(can_config_t, enable_tdc),
                offsetof(DownFrame, buf), offsetof(can_transmit_buf_t, data),
                offsetof(can_receive_buf_t, data));
    DownFrame f{};
    f.board_id = 121;
    f.can_channel_id = 0;
    f.buf.id = 1 | (2 << 5);
    f.buf.bitrate_switch = 1;
    f.buf.canfd_frame = 1;
    const auto* bytes = reinterpret_cast<const unsigned char*>(&f);
    std::printf("enable_payload=");
    for (size_t i = 0; i < sizeof(f); ++i)
        std::printf("%02x", bytes[i]);
    std::puts("");
}
