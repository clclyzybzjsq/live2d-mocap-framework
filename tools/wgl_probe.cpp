// wgl_probe —— 验证用原生 WGL 能否拿到 Compatibility Profile 上下文。
//
// 背景：Qt Widgets 应用里，QOpenGLContext 即使显式请求 CompatibilityProfile
// 也只能拿到 Core Profile（实测 profile=2），而 Cubism 的 OpenGL ES2 后端在
// Core Profile 下渲染出空白画面。这里绕过 Qt 的格式协商，直接用 WGL 创建上下文。
#include <windows.h>

#include <GL/glew.h>

#include <cstdio>

// WGL 扩展常量（不在标准头文件里）
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x00000001
#define WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB 0x00000002

typedef HGLRC(WINAPI* PFNWGLCREATECONTEXTATTRIBSARBPROC)(HDC, HGLRC, const int*);

namespace {

const char* profile_of() {
    GLint mask = 0;
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &mask);
    if (mask & GL_CONTEXT_COMPATIBILITY_PROFILE_BIT) return "Compatibility";
    if (mask & GL_CONTEXT_CORE_PROFILE_BIT) return "Core";
    return "未知/无";
}

bool try_wgl(int major, int minor, int profile_bit, const char* label) {
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"LbWglProbe";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, L"LbWglProbe", L"", WS_OVERLAPPEDWINDOW, 0, 0, 8, 8, nullptr,
                                nullptr, wc.hInstance, nullptr);
    if (!hwnd) {
        printf("  %-22s -> 创建窗口失败\n", label);
        return false;
    }
    HDC dc = GetDC(hwnd);

    PIXELFORMATDESCRIPTOR pfd = {};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    pfd.cDepthBits = 24;
    pfd.cStencilBits = 8;
    const int pf = ChoosePixelFormat(dc, &pfd);
    if (pf == 0 || !SetPixelFormat(dc, pf, &pfd)) {
        printf("  %-22s -> 像素格式设置失败\n", label);
        ReleaseDC(hwnd, dc);
        DestroyWindow(hwnd);
        return false;
    }

    // 先建临时上下文，才能取到 wglCreateContextAttribsARB
    HGLRC tmp = wglCreateContext(dc);
    if (!tmp || !wglMakeCurrent(dc, tmp)) {
        printf("  %-22s -> 临时上下文失败\n", label);
        ReleaseDC(hwnd, dc);
        DestroyWindow(hwnd);
        return false;
    }
    auto create_attribs = reinterpret_cast<PFNWGLCREATECONTEXTATTRIBSARBPROC>(
        wglGetProcAddress("wglCreateContextAttribsARB"));
    if (!create_attribs) {
        printf("  %-22s -> 驱动不支持 wglCreateContextAttribsARB\n", label);
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(tmp);
        ReleaseDC(hwnd, dc);
        DestroyWindow(hwnd);
        return false;
    }

    const int attribs[] = {
        WGL_CONTEXT_MAJOR_VERSION_ARB, major,
        WGL_CONTEXT_MINOR_VERSION_ARB, minor,
        WGL_CONTEXT_PROFILE_MASK_ARB,  profile_bit,
        0,
    };
    HGLRC glrc = create_attribs(dc, nullptr, attribs);
    if (!glrc) {
        printf("  %-22s -> 创建失败（驱动拒绝该组合）\n", label);
        wglMakeCurrent(nullptr, nullptr);
        wglDeleteContext(tmp);
        ReleaseDC(hwnd, dc);
        DestroyWindow(hwnd);
        return false;
    }
    wglMakeCurrent(dc, glrc);

    glewExperimental = GL_TRUE;
    glewInit();
    glGetError();

    const auto* ver = reinterpret_cast<const char*>(glGetString(GL_VERSION));
    const auto* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
    printf("  %-22s -> %s | %s | profile=%s\n", label, ver ? ver : "?", renderer ? renderer : "?",
           profile_of());

    wglMakeCurrent(nullptr, nullptr);
    wglDeleteContext(glrc);
    wglDeleteContext(tmp);
    ReleaseDC(hwnd, dc);
    DestroyWindow(hwnd);
    return true;
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("WGL 上下文探测（绕过 Qt 直接创建）\n");
    try_wgl(4, 6, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB, "4.6 Compatibility");
    try_wgl(4, 3, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB, "4.3 Compatibility");
    try_wgl(3, 2, WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB, "3.2 Compatibility");
    try_wgl(4, 6, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, "4.6 Core（对照）");
    return 0;
}
