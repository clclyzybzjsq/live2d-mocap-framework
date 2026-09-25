// vcam_write_test —— WinVirtualCamera 写端生命周期测试（不需注册/提权）。
// start -> 写入 BGRA 测试帧 -> 校验共享内存(YUY2) -> stop。
// 再与 filter 消费（lb_vcam_flow 已单独验证），本工具只验写端与协议内容。
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "lb/win/win_virtual_camera.h"
#include "lb/core/vcam_protocol.h"

using lb::core::FrameView;

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("vcam write test start\n");

    lb::win::WinVirtualCamera vcam;

    // 1) start
    lb::core::VirtualCameraFormat fmt;
    fmt.width = 640;
    fmt.height = 480;
    fmt.fps = 30;
    if (!vcam.start(fmt)) {
        printf("FAIL: start\n");
        return 1;
    }
    printf("started, is_running=%d\n", vcam.is_running() ? 1 : 0);

    // 2) 构造 BGRA 测试帧（左半红右半蓝，便于校验 YUY2 内容）
    std::vector<uint8_t> bgra;
    const size_t pitch = 640u * 4u;
    bgra.resize(pitch * 480);
    for (int y = 0; y < 480; ++y) {
        for (int x = 0; x < 640; ++x) {
            uint8_t* p = bgra.data() + static_cast<size_t>(y) * pitch + static_cast<size_t>(x) * 4;
            if (x < 320) { p[0] = 0; p[1] = 0; p[2] = 255; p[3] = 255; } // 红
            else         { p[0] = 255; p[1] = 0; p[2] = 0; p[3] = 255; } // 蓝
        }
    }
    FrameView frame;
    frame.plane0 = bgra.data();
    frame.stride0 = static_cast<int>(pitch);
    frame.width = 640;
    frame.height = 480;
    frame.format = lb::core::PixelFormat::BGRA32;
    frame.timestamp_us = 1234567;

    for (int i = 0; i < 5; ++i) {
        if (!vcam.push_frame(frame)) {
            printf("FAIL: push_frame %d\n", i);
            return 1;
        }
    }
    printf("pushed 5 frames\n");

    // 3) 打开共享内存校验内容
    HANDLE map = OpenFileMappingW(FILE_MAP_ALL_ACCESS, FALSE, L"lb_vcam_frames");
    if (!map) {
        printf("FAIL: open shm err=%lu\n", GetLastError());
        return 1;
    }
    void* view = MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, lb::core::vcam_mapping_bytes());
    if (!view) {
        printf("FAIL: map shm\n");
        CloseHandle(map);
        return 1;
    }
    const auto* hdr = static_cast<const lb::core::SharedHeaderVcam*>(view);
    const uint8_t* data = lb::core::vcam_frame_data(hdr);

    const bool ok =
        hdr->magic == lb::core::kVcamMagic &&
        hdr->version == lb::core::kVcamVersion &&
        hdr->state == lb::core::kVcamRunning &&
        hdr->frame_index == 5 &&
        hdr->width == 640 && hdr->height == 480 && hdr->pitch == 1280 &&
        hdr->timestamp_us == 1234567 &&
        data[0] == 0x52 /* Y(红)=82 */ && data[1] == 0x5A /* U(红)=90 */ &&
        data[3] == 0xF0 /* V(红)=240 */ &&
        data[1280 * 480 - 4] != 0; // 末行存在内容

    if (ok) {
        printf("RESULT OK: frame_index=%llu first_yu=%02X,%02X\n",
               static_cast<unsigned long long>(hdr->frame_index), data[0], data[1]);
    } else {
        printf("RESULT FAIL: magic=%08X ver=%u state=%u idx=%llu w=%u h=%u pitch=%u ts=%llu\n",
               hdr->magic, hdr->version, hdr->state,
               static_cast<unsigned long long>(hdr->frame_index), hdr->width, hdr->height,
               hdr->pitch, static_cast<unsigned long long>(hdr->timestamp_us));
        printf("  first bytes: %02X %02X %02X %02X (Y,U,Y,V expected 52 5A 52 F0)\n",
               data[0], data[1], data[2], data[3]);
    }

    UnmapViewOfFile(view);
    CloseHandle(map);

    printf("has_consumer=%d\n", vcam.has_consumer() ? 1 : 0);
    vcam.stop();
    printf("stopped, is_running=%d\n", vcam.is_running() ? 1 : 0);
    return ok ? 0 : 1;
}