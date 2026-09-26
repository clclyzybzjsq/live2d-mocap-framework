// vcam_feed —— 持续向虚拟摄像头共享内存喂帧，模拟应用写端
// （与 UI 的动捕推帧走同一条 WinVirtualCamera 路径：BGRA -> YUY2 -> 共享内存）。
//
// 用途：验证「应用写端 -> lb_vcam_frames -> 消费方(ffmpeg/OBS)」完整链路。
// 图案为左半红、右半蓝，便于与 filter 无写端时的彩条回退区分。
//
// 用法: lb_vcam_feed [持续秒数]   默认 8 秒
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "lb/core/vcam_protocol.h"
#include "lb/win/win_virtual_camera.h"

using lb::core::FrameView;

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    const int seconds = (argc > 1) ? atoi(argv[1]) : 8;
    printf("vcam feed start (%d s)\n", seconds);

    lb::win::WinVirtualCamera vcam;
    lb::core::VirtualCameraFormat fmt;
    fmt.width = 640;
    fmt.height = 480;
    fmt.fps = 30;
    fmt.preferred_format = lb::core::PixelFormat::YUY2;
    if (!vcam.start(fmt)) {
        printf("FAIL: start\n");
        return 1;
    }

    // BGRA 测试帧：左半红、右半蓝
    const int w = 640, h = 480;
    const size_t pitch = static_cast<size_t>(w) * 4u;
    std::vector<uint8_t> bgra(pitch * static_cast<size_t>(h));
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uint8_t* p = bgra.data() + static_cast<size_t>(y) * pitch + static_cast<size_t>(x) * 4;
            if (x < w / 2) { p[0] = 0; p[1] = 0; p[2] = 255; p[3] = 255; }      // 红
            else           { p[0] = 255; p[1] = 0; p[2] = 0; p[3] = 255; }      // 蓝
        }
    }

    FrameView frame;
    frame.plane0 = bgra.data();
    frame.stride0 = static_cast<int>(pitch);
    frame.width = w;
    frame.height = h;
    frame.format = lb::core::PixelFormat::BGRA32;

    const int total = seconds * 30;
    int pushed = 0;
    for (int i = 0; i < total; ++i) {
        frame.timestamp_us = static_cast<uint64_t>(i) * 33333ull;
        if (!vcam.push_frame(frame)) {
            printf("FAIL: push_frame at %d\n", i);
            vcam.stop();
            return 1;
        }
        ++pushed;
        Sleep(33);
    }

    printf("pushed %d frames, has_consumer=%d\n", pushed, vcam.has_consumer() ? 1 : 0);
    vcam.stop();
    printf("RESULT OK\n");
    return 0;
}
