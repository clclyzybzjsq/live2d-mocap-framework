// gl_format_probe —— 探测 Qt 在 Windows 上能拿到哪种 GL 上下文。
// Cubism 的 OpenGL ES2 后端需要兼容 Profile（Core Profile 下大量调用无效），
// 而不同 (版本, Profile) 组合在 Intel 驱动上的实际结果差异很大，这里逐一验证。
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>

#include <cstdio>

namespace {

const char* profile_name(QSurfaceFormat::OpenGLContextProfile p) {
    switch (p) {
        case QSurfaceFormat::CoreProfile: return "Core";
        case QSurfaceFormat::CompatibilityProfile: return "Compatibility";
        default: return "NoProfile";
    }
}

void try_format(int major, int minor, QSurfaceFormat::OpenGLContextProfile profile) {
    QSurfaceFormat fmt;
    if (major > 0) fmt.setVersion(major, minor);
    fmt.setProfile(profile);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);

    QOpenGLContext ctx;
    ctx.setFormat(fmt);
    if (!ctx.create()) {
        printf("  %d.%d %-14s -> create 失败\n", major, minor, profile_name(profile));
        return;
    }
    QOffscreenSurface surface;
    surface.setFormat(ctx.format());
    surface.create();
    if (!surface.isValid() || !ctx.makeCurrent(&surface)) {
        printf("  %d.%d %-14s -> surface/makeCurrent 失败\n", major, minor, profile_name(profile));
        return;
    }
    const QSurfaceFormat got = ctx.format();
    printf("  %d.%d %-14s -> 实际 %d.%d %s  (VAO 可用=%d)\n", major, minor, profile_name(profile),
           got.majorVersion(), got.minorVersion(), profile_name(got.profile()),
           got.version() >= qMakePair(3, 0) ? 1 : 0);
    ctx.doneCurrent();
}

} // namespace

int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("GL 格式探测（当前驱动/显卡）\n");

    // 不指定版本
    try_format(0, 0, QSurfaceFormat::NoProfile);
    try_format(0, 0, QSurfaceFormat::CompatibilityProfile);
    try_format(0, 0, QSurfaceFormat::CoreProfile);
    // 逐版本 + 兼容
    try_format(2, 1, QSurfaceFormat::NoProfile);
    try_format(3, 0, QSurfaceFormat::CompatibilityProfile);
    try_format(3, 1, QSurfaceFormat::CompatibilityProfile);
    try_format(3, 2, QSurfaceFormat::CompatibilityProfile);
    try_format(3, 3, QSurfaceFormat::CompatibilityProfile);
    try_format(4, 0, QSurfaceFormat::CompatibilityProfile);
    try_format(4, 3, QSurfaceFormat::CompatibilityProfile);
    try_format(4, 6, QSurfaceFormat::CompatibilityProfile);
    // 交给 Qt 默认
    {
        QOpenGLContext ctx;
        if (ctx.create()) {
            QOffscreenSurface surface;
            surface.setFormat(ctx.format());
            surface.create();
            if (surface.isValid() && ctx.makeCurrent(&surface)) {
                const QSurfaceFormat got = ctx.format();
                printf("  Qt 默认格式      -> 实际 %d.%d %s\n", got.majorVersion(), got.minorVersion(),
                       profile_name(got.profile()));
                ctx.doneCurrent();
            }
        }
    }
    return 0;
}
