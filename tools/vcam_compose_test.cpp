// vcam_compose_test —— 离屏验证虚拟摄像头合成器（不需要 UI/相机/设备）。
// 用一个高频棋盘格当假相机帧，检查：
//   1) 默认模式：背景被显著模糊（相邻像素差能量大幅下降）
//   2) 默认模式：中心区域出现 Live2D 头像特征色（灰度 ~120）
//   3) 仅背景模式：同位置不再是头像色，且仍保持模糊背景
#include <QCoreApplication>
#include <QImage>

#include <windows.h>

#include <cmath>
#include <cstdio>
#include <vector>

#include "lb/core/semantic_params.h"
#include "lb/core/types.h"
#include "render/vcam_compositor.h"

namespace {

// 相邻像素梯度能量：衡量画面高频细节（模糊后应显著下降）
double gradient_energy(const uint8_t* data, int w, int h, uint32_t pitch) {
    double sum = 0.0;
    long n = 0;
    for (int y = 2; y < h - 2; y += 2) {
        const uint8_t* row = data + static_cast<size_t>(y) * pitch;
        for (int x = 2; x < w - 2; x += 2) {
            const int i = x * 4;
            sum += std::abs(static_cast<int>(row[i]) - static_cast<int>(row[i - 4]));
            sum += std::abs(static_cast<int>(row[i]) - static_cast<int>(row[i + 4]));
            n += 2;
        }
    }
    return n ? sum / static_cast<double>(n) : 0.0;
}

struct Rgb {
    int r, g, b;
};

Rgb sample(const uint8_t* data, uint32_t pitch, int x, int y) {
    const uint8_t* p = data + static_cast<size_t>(y) * pitch + static_cast<size_t>(x) * 4;
    return Rgb{p[2], p[1], p[0]};
}

} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    setvbuf(stdout, nullptr, _IONBF, 0);

    const int w = 640, h = 480;
    const uint32_t pitch = static_cast<uint32_t>(w) * 4u;

    // 假相机帧：8 像素棋盘格（高频细节，便于观察模糊效果）
    std::vector<uint8_t> camera(static_cast<size_t>(pitch) * h);
    for (int y = 0; y < h; ++y) {
        uint8_t* row = camera.data() + static_cast<size_t>(y) * pitch;
        for (int x = 0; x < w; ++x) {
            const bool on = ((x / 8) + (y / 8)) % 2 == 0;
            const uint8_t v = on ? 235 : 20;
            row[x * 4 + 0] = v;
            row[x * 4 + 1] = v;
            row[x * 4 + 2] = v;
            row[x * 4 + 3] = 255;
        }
    }

    lb::core::FrameView cam;
    cam.plane0 = camera.data();
    cam.stride0 = pitch;
    cam.width = w;
    cam.height = h;
    cam.format = lb::core::PixelFormat::BGRA32;

    lb::core::SemanticParams sem; // 默认中立姿态
    const double cam_energy = gradient_energy(camera.data(), w, h, pitch);

    lb::ui::VcamCompositor comp;

    // ---- 1) 默认：Live2D + 背景虚化 ----
    comp.set_content(lb::ui::VcamContent::Live2dOverBlurredBackground);
    const lb::core::FrameView* out = comp.compose(cam, sem);
    if (!out) {
        printf("FAIL: compose returned null\n");
        return 1;
    }
    const auto* px = static_cast<const uint8_t*>(out->plane0);
    const double default_energy = gradient_energy(px, w, h, out->stride0);
    const Rgb center = sample(px, out->stride0, w / 2, h / 2);
    const Rgb corner = sample(px, out->stride0, 10, 10);
    const Rgb body = sample(px, out->stride0, w / 2, static_cast<int>(h * 0.42 + 170));
    printf("默认模式 (Live2D + 背景虚化)\n");
    printf("  背景梯度能量: 输入 %.2f -> 输出 %.2f (下降 %.0f%%)\n", cam_energy, default_energy,
           100.0 * (1.0 - default_energy / cam_energy));
    printf("  中心(头像) RGB = %d,%d,%d  期望接近 120,116,118\n", center.r, center.g, center.b);
    printf("  躯干 RGB = %d,%d,%d  期望接近 64,64,72\n", body.r, body.g, body.b);
    printf("  角落(背景) RGB = %d,%d,%d\n", corner.r, corner.g, corner.b);

    const bool blur_ok = default_energy < cam_energy * 0.5;
    const bool avatar_ok = std::abs(center.r - 120) < 24 && std::abs(center.g - 116) < 24;

    // ---- 2) 仅背景（默认关闭的选项打开后） ----
    comp.set_content(lb::ui::VcamContent::BackgroundOnly);
    out = comp.compose(cam, sem);
    const auto* px2 = static_cast<const uint8_t*>(out->plane0);
    const double bg_energy = gradient_energy(px2, w, h, out->stride0);
    const Rgb center2 = sample(px2, out->stride0, w / 2, h / 2);
    printf("仅背景模式 (无 Live2D)\n");
    printf("  背景梯度能量: %.2f\n", bg_energy);
    printf("  中心 RGB = %d,%d,%d  期望不再是头像灰\n", center2.r, center2.g, center2.b);
    const bool bg_only_ok = bg_energy < cam_energy * 0.5 &&
                            (std::abs(center2.r - center.r) > 12 || std::abs(center2.g - center.g) > 12);

    // ---- 3) 合成耗时（在管线回调线程内同步执行，必须足够快） ----
    lb::ui::VcamCompositor perf;
    perf.set_content(lb::ui::VcamContent::Live2dOverBlurredBackground);
    perf.compose(cam, sem); // 预热
    const int kN = 60;
    const ULONGLONG t0 = GetTickCount64();
    for (int i = 0; i < kN; ++i) perf.compose(cam, sem);
    const double ms = static_cast<double>(GetTickCount64() - t0) / kN;
    printf("合成耗时: %.2f ms/帧 (640x480，含模糊+压暗+形象绘制+拷贝)\n", ms);
    const bool perf_ok = ms < 8.0;

    printf("\n结论:\n");
    printf("  背景虚化生效: %s\n", blur_ok ? "是" : "否");
    printf("  Live2D 形象存在: %s\n", avatar_ok ? "是" : "否");
    printf("  仅背景模式与默认模式可区分: %s\n", bg_only_ok ? "是" : "否");
    printf("  性能可接受(<8ms): %s\n", perf_ok ? "是" : "否");

    const bool all_ok = blur_ok && avatar_ok && bg_only_ok && perf_ok;
    printf("%s\n", all_ok ? "RESULT OK" : "RESULT FAIL");
    return all_ok ? 0 : 1;
}
