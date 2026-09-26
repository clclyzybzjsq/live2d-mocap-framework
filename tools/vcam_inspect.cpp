// vcam_inspect —— 只读检查虚拟摄像头共享内存状态（不创建、不干扰写端）。
// 用途：确认应用（UI）是否已启动虚拟摄像头、是否在推帧、画面内容大概是什么。
// 用法: lb_vcam_inspect
#include <windows.h>

#include <cstdint>
#include <cstdio>

#include "lb/core/vcam_protocol.h"

namespace {

struct Yuv {
    int y, u, v;
};

// 采样共享内存中一个 YUY2 像素（x 为偶数对齐的像素对）
Yuv sample_yuy2(const uint8_t* frame, uint32_t pitch, uint32_t x, uint32_t y) {
    const uint8_t* p = frame + static_cast<size_t>(y) * pitch + (x & ~1u) * 2u;
    const int y0 = p[0], u = p[1], y1 = p[2], v = p[3];
    Yuv out{};
    out.y = (x & 1u) ? y1 : y0;
    out.u = u;
    out.v = v;
    return out;
}

void yuv_to_rgb(const Yuv& c, int* r, int* g, int* b) {
    // BT.601 逆变换
    const double y = c.y, u = c.u - 128.0, v = c.v - 128.0;
    *r = static_cast<int>(y + 1.402 * v);
    *g = static_cast<int>(y - 0.344136 * u - 0.714136 * v);
    *b = static_cast<int>(y + 1.772 * u);
    if (*r < 0) *r = 0; if (*r > 255) *r = 255;
    if (*g < 0) *g = 0; if (*g > 255) *g = 255;
    if (*b < 0) *b = 0; if (*b > 255) *b = 255;
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("vcam inspect\n");

    HANDLE map = OpenFileMappingW(FILE_MAP_READ, FALSE, L"lb_vcam_frames");
    if (!map) {
        printf("状态: 未运行 —— 共享内存 lb_vcam_frames 不存在\n");
        printf("      （UI 的“虚拟摄像头”开关未打开，或已停止）\n");
        return 1;
    }
    const void* view = MapViewOfFile(map, FILE_MAP_READ, 0, 0, lb::core::vcam_mapping_bytes());
    if (!view) {
        printf("状态: 映射失败 err=%lu\n", GetLastError());
        CloseHandle(map);
        return 1;
    }
    const auto* hdr = static_cast<const lb::core::SharedHeaderVcam*>(view);

    printf("状态: 写端在运行\n");
    printf("  magic=%08X (期望 %08X)  version=%u\n", hdr->magic, lb::core::kVcamMagic,
           hdr->version);
    printf("  state=%u (%s)\n", hdr->state,
           hdr->state == lb::core::kVcamRunning ? "running" : "idle");
    printf("  尺寸=%ux%u  pitch=%u  format=%u (1=YUY2)\n", hdr->width, hdr->height,
           hdr->pitch, hdr->format);
    printf("  消费方数量 consumer_count=%d\n", hdr->consumer_count);

    const uint64_t idx0 = hdr->frame_index;
    printf("  当前 frame_index=%llu\n", static_cast<unsigned long long>(idx0));
    Sleep(800);
    const uint64_t idx1 = hdr->frame_index;
    const bool advancing = idx1 > idx0;
    printf("  800ms 后 frame_index=%llu  -> %s\n", static_cast<unsigned long long>(idx1),
           advancing ? "正在推帧" : "未推帧（静止）");
    if (advancing) {
        printf("  推定帧率≈%.1f fps\n", static_cast<double>(idx1 - idx0) / 0.8);
    }

    if (hdr->magic == lb::core::kVcamMagic && hdr->width && hdr->height && idx1 > 0) {
        const uint8_t* frame = lb::core::vcam_frame_data(hdr);
        struct Probe {
            const char* name;
            uint32_t x, y;
        };
        const Probe probes[] = {
            {"左上",   hdr->width / 8,     hdr->height / 8},
            {"中心",   hdr->width / 2,     hdr->height / 2},
            {"右上",   hdr->width * 7 / 8, hdr->height / 8},
            {"左下",   hdr->width / 8,     hdr->height * 7 / 8},
        };
        printf("画面采样（YUY2 -> RGB 近似）:\n");
        for (const Probe& pr : probes) {
            const Yuv c = sample_yuy2(frame, hdr->pitch, pr.x, pr.y);
            int r = 0, g = 0, b = 0;
            yuv_to_rgb(c, &r, &g, &b);
            printf("  %s (%u,%u): Y=%3d U=%3d V=%3d -> RGB(%3d,%3d,%3d)\n", pr.name, pr.x, pr.y,
                   c.y, c.u, c.v, r, g, b);
        }
    }

    UnmapViewOfFile(view);
    CloseHandle(map);
    printf("结论: %s\n", advancing ? "虚拟摄像头正在输出画面" : "已启动但当前没有帧（需启动动捕/相机）");
    return 0;
}
