#include "wgl_context.h"

// glew.h 必须先于其它 GL 头
#include <GL/glew.h>

#include <cstdio>

// WGL 扩展常量与函数指针（不在标准头文件里）
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002

typedef HGLRC(WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int*);

namespace lb::ui {

namespace {
constexpr const wchar_t* kWindowClass = L"LbOffscreenGlWindow";
}

WglOffscreenContext::~WglOffscreenContext() { destroy(); }

bool WglOffscreenContext::create(int major, int minor) {
    destroy();

    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = kWindowClass;
    RegisterClassW(&wc); // 已注册时返回 0，忽略即可
    hwnd_ = CreateWindowExW(0, kWindowClass, L"", WS_OVERLAPPEDWINDOW, 0, 0, 8, 8, nullptr, nullptr,
                            wc.hInstance, nullptr);
    if (!hwnd_) {
        std::fprintf(stderr, "[wgl] 创建离屏窗口失败\n");
        return false;
    }
    dc_ = GetDC(hwnd_);
    if (!dc_) {
        std::fprintf(stderr, "[wgl] 获取 DC 失败\n");
        destroy();
        return false;
    }

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8; // Cubism 的剪裁遮罩需要模板缓冲
    const int pf = ChoosePixelFormat(dc_, &pfd);
    if (pf == 0 || !SetPixelFormat(dc_, pf, &pfd)) {
        std::fprintf(stderr, "[wgl] 像素格式设置失败\n");
        destroy();
        return false;
    }

    // 需要先有 current 上下文才能取到 wglCreateContextAttribsARB
    HGLRC tmp = wglCreateContext(dc_);
    if (!tmp || !wglMakeCurrent(dc_, tmp)) {
        std::fprintf(stderr, "[wgl] 临时上下文创建失败\n");
        destroy();
        return false;
    }
    auto create_attribs = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
        wglGetProcAddress("wglCreateContextAttribsARB"));
    if (!create_attribs) {
        std::fprintf(stderr, "[wgl] 驱动不支持 wglCreateContextAttribsARB\n");
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(tmp);
        destroy();
        return false;
    }

    const int attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, major,
        WGL_CONTEXT_MINOR_VERSION_ARB, minor,
        WGL_CONTEXT_PROFILE_MASK_ARB,  WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
        0,
    };
    glrc_ = create_attribs(dc_, nullptr, attribs);
    if (!glrc_) {
        // 退回到传统的 wglCreateContext（历史上它就是兼容上下文）
        std::fprintf(stderr, "[wgl] 兼容 Profile 创建失败，回退 wglCreateContext\n");
        glrc_ = tmp;
        wglMakeCurrent(dc_, glrc_);
    } else {
        wglMakeCurrent(dc_, glrc_);
        wglDeleteContext(tmp);
    }

    glGetError();
    return true;
}

bool WglOffscreenContext::make_current() {
    if (!glrc_ || !dc_) return false;
    if (!wglMakeCurrent(dc_, glrc_)) return false;
    return true;
}

void WglOffscreenContext::done_current() { wglMakeCurrent(nullptr, nullptr); }

const char* WglOffscreenContext::profile_name() const {
    GLint mask = 0;
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &mask);
    if (mask & GL_CONTEXT_COMPATIBILITY_PROFILE_BIT) return "Compatibility";
    if (mask & GL_CONTEXT_CORE_PROFILE_BIT) return "Core";
    return "未知";
}

void WglOffscreenContext::destroy() {
    if (glrc_) {
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(glrc_);
        glrc_ = nullptr;
    }
    if (dc_) {
        ReleaseDC(hwnd_, dc_);
        dc_ = nullptr;
    }
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
}

} // namespace lb::ui
