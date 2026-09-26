// cubism_renderer_test —— 验证 CubismAvatarRenderer 的完整路径（不启动 UI）。
// 覆盖：模型库解析 -> create_avatar_renderer（自动选择 Cubism 渲染器）->
//       UI 线程 tick 渲染 -> 管线线程视角 render 读取缓存 -> 画布内容与性能校验。
#include <QGuiApplication>
#include <QImage>

#include <chrono>
#include <cmath>
#include <cstdio>

#include "lb/core/semantic_params.h"
#include "render/avatar_renderer.h"
#include "render/cubism_avatar_renderer.h"
#include "render/live2d_model_library.h"
#include "render/vcam_compositor.h"

using namespace lb::ui;

namespace {

int g_fail = 0;

void check(bool ok, const char* what) {
    printf("  [%s] %s\n", ok ? "OK" : "FAIL", what);
    if (!ok) ++g_fail;
}

struct Stats {
    double coverage = 0.0;
    int distinct = 0;
};

Stats analyze(const QImage& img) {
    Stats s;
    long opaque = 0;
    bool seen[4096] = {false};
    int distinct = 0;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            const QRgb p = line[x];
            if (qAlpha(p) > 8) ++opaque;
            const int idx = ((qRed(p) >> 4) << 8) | ((qGreen(p) >> 4) << 4) | (qBlue(p) >> 4);
            if (!seen[idx]) {
                seen[idx] = true;
                ++distinct;
            }
        }
    }
    const long total = static_cast<long>(img.width()) * img.height();
    s.coverage = total ? static_cast<double>(opaque) / total : 0.0;
    s.distinct = distinct;
    return s;
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    setvbuf(stdout, nullptr, _IONBF, 0);

    const QStringList args = QGuiApplication::arguments();
    if (args.size() < 2) {
        printf("用法: lb_cubism_renderer_test <model3.json> [输出png] [zoom] [offsetY]\n");
        return 1;
    }

    printf("cubism renderer test\n");
    const Live2dModelInfo model = Live2dModelLibrary::parse(args.at(1));
    printf("  模型: %s (moc3=%s)\n", model.name.toUtf8().constData(),
           model.moc3_exists ? "有" : "无");

    std::unique_ptr<AvatarRenderer> renderer = create_avatar_renderer(model);
    if (!renderer) {
        printf("FAIL: create_avatar_renderer 返回空\n");
        return 1;
    }
    printf("  渲染器: %s (cubism_available=%d)\n", renderer->name().toUtf8().constData(),
           cubism_available() ? 1 : 0);
    check(renderer->name() == QStringLiteral("Cubism"), "已选用 Cubism 渲染器（非占位回退）");

    // 构图参数（可选）：lb_cubism_renderer_test <model> <png> <zoom> <offsetY>
    if (auto* cubism = dynamic_cast<CubismAvatarRenderer*>(renderer.get())) {
        if (args.size() >= 4) cubism->set_zoom(args.at(3).toFloat());
        if (args.size() >= 5) cubism->set_offset(0.0f, args.at(4).toFloat());
    }

    // 语义参数：带明显头部转动与眨眼，便于观察
    lb::core::SemanticParams sem;
    sem.head.yaw = 12.0f;
    sem.head.pitch = -6.0f;
    sem.head.roll = 4.0f;
    sem.eye.open_left = 1.0f;
    sem.eye.open_right = 0.6f;
    sem.mouth.open = 0.4f;
    sem.mouth.smile = 0.5f;
    sem.body.torso_sway = 3.0f;

    // 可选参数：zero 表示使用全零语义参数（复现 UI 未启动动捕的情形）
    if (args.size() >= 6 && args.at(5) == QStringLiteral("zero")) {
        sem = lb::core::SemanticParams{};
        printf("  （使用全零语义参数）\n");
    }

    // 模拟：UI 线程 tick 渲染若干帧
    const int kWarm = 3;
    for (int i = 0; i < kWarm; ++i) renderer->tick(sem);

    const int kN = 20;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kN; ++i) renderer->tick(sem);
    const double tick_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count() / kN;
    printf("  tick 平均耗时: %.2f ms/帧（含 GL 上下文切换与像素读回）\n", tick_ms);

    // 模拟：管线线程视角读取（不得触碰 GL）
    QImage canvas(640, 480, QImage::Format_ARGB32);
    canvas.fill(Qt::transparent);
    const auto t1 = std::chrono::steady_clock::now();
    renderer->render(canvas, sem);
    const double render_ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t1).count();
    printf("  render（读缓存）耗时: %.2f ms\n", render_ms);

    const Stats s = analyze(canvas);
    printf("  画布非透明占比: %.1f%%  颜色种类: %d\n", s.coverage * 100.0, s.distinct);
    check(s.coverage > 0.05, "画布有模型内容");
    check(s.distinct > 20, "画布有细节（真实渲染而非单色）");
    check(render_ms < 8.0, "管线线程读取开销可接受（< 8ms）");

    // 动效验证：呼吸/物理应让画面随时间产生变化（同一语义参数下）
    QImage frame_a(640, 480, QImage::Format_ARGB32);
    frame_a.fill(Qt::transparent);
    renderer->render(frame_a, sem);
    for (int i = 0; i < 40; ++i) renderer->tick(sem); // 约 1.3 秒
    QImage frame_b(640, 480, QImage::Format_ARGB32);
    frame_b.fill(Qt::transparent);
    renderer->render(frame_b, sem);
    {
        long d = 0, n = 0;
        for (int y = 0; y < frame_a.height(); ++y) {
            const QRgb* la = reinterpret_cast<const QRgb*>(frame_a.constScanLine(y));
            const QRgb* lb = reinterpret_cast<const QRgb*>(frame_b.constScanLine(y));
            for (int x = 0; x < frame_a.width(); ++x) {
                d += std::abs(qRed(la[x]) - qRed(lb[x])) + std::abs(qGreen(la[x]) - qGreen(lb[x])) +
                     std::abs(qBlue(la[x]) - qBlue(lb[x]));
                n += 3;
            }
        }
        const double diff = n ? static_cast<double>(d) / n : 0.0;
        printf("  动效帧间差异: %.3f (0-255)\n", diff);
        check(diff > 0.02, "呼吸/物理动效生效（画面随时间变化）");
    }

    // 虚拟摄像头合成路径：Cubism 形象 + 虚化背景
    VcamCompositor compositor;
    compositor.set_avatar_renderer(renderer.get());
    lb::core::FrameView camera;
    static std::vector<unsigned char> cam_buf(640u * 480u * 4u);
    for (size_t i = 0; i < cam_buf.size(); i += 4) {
        cam_buf[i] = 90;
        cam_buf[i + 1] = 110;
        cam_buf[i + 2] = 130;
        cam_buf[i + 3] = 255;
    }
    camera.plane0 = cam_buf.data();
    camera.stride0 = 640u * 4u;
    camera.width = 640;
    camera.height = 480;
    camera.format = lb::core::PixelFormat::BGRA32;
    const lb::core::FrameView* composited = compositor.compose(camera, sem);
    if (!composited) {
        printf("FAIL: 合成失败\n");
        return 1;
    }
    const QImage out(static_cast<const uchar*>(composited->plane0), composited->width,
                     composited->height, static_cast<int>(composited->stride0),
                     QImage::Format_ARGB32);
    const Stats cs = analyze(out);
    printf("  合成输出: %dx%d 非透明占比 %.1f%%\n", out.width(), out.height(),
           cs.coverage * 100.0);
    check(cs.coverage > 0.95, "合成输出铺满画面（虚化背景 + 模型）");

    if (args.size() >= 3) {
        const QString out_png = args.at(2);
        out.save(out_png);
        printf("  已保存合成结果: %s\n", out_png.toUtf8().constData());
    }

    printf("\n%s\n", g_fail == 0 ? "RESULT OK" : "RESULT FAIL");
    return g_fail == 0 ? 0 : 1;
}
