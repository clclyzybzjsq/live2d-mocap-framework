#include "lb/win/win_gpu_context.h"

namespace lb::win {

bool WinGpuContext::initialize() {
    // TODO(M1): D3D11CreateDevice；浮窗路径创建 DirectComposition device 与 visual。
    initialized_ = false;
    return false;
}

void WinGpuContext::shutdown() {
    initialized_ = false;
}

lb::core::ITexture2D* WinGpuContext::create_texture(int width, int height) {
    // TODO(M1): 创建可共享纹理（共享句柄供虚拟摄像头/浮窗使用）。
    (void)width;
    (void)height;
    return nullptr;
}

void WinGpuContext::destroy_texture(lb::core::ITexture2D* texture) {
    (void)texture;
}

void WinGpuContext::begin_frame() {
    // TODO(M1): 绑定渲染目标、清屏。
}

void WinGpuContext::end_frame() {
    // TODO(M1): 提交命令、呈现。
}

} // namespace lb::win