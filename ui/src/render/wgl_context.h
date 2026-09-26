#pragma once

#include <windows.h>

namespace lb::ui {

// 原生 WGL 离屏 OpenGL 上下文。
//
// 为什么不用 Qt 的 QOpenGLContext：在 Qt Widgets 应用里，即使显式请求
// CompatibilityProfile（QSurfaceFormat），实际拿到的仍是 Core Profile ——
// 而 Cubism 的 OpenGL ES2 后端在 Core Profile 下会渲染出空白画面
// （详见 docs/assessment-plan.md M3 节）。原生 WGL 能正确拿到兼容上下文
// （tools/wgl_probe 已验证）。
class WglOffscreenContext {
public:
    WglOffscreenContext() = default;
    ~WglOffscreenContext();

    WglOffscreenContext(const WglOffscreenContext&) = delete;
    WglOffscreenContext& operator=(const WglOffscreenContext&) = delete;

    // 创建隐藏窗口与兼容 Profile 上下文；成功后上下文为 current
    bool create(int major = 4, int minor = 6);

    bool make_current();
    void done_current();

    // 当前上下文的 Profile 描述（诊断用）
    const char* profile_name() const;

private:
    void destroy();

    HWND hwnd_ = nullptr;
    HDC dc_ = nullptr;
    HGLRC glrc_ = nullptr;
    bool compatible_ = false;
};

} // namespace lb::ui
