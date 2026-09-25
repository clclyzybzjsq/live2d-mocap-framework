#include "lb/win/win_floating_window.h"

namespace lb::win {

bool WinFloatingWindow::create() {
    // TODO(M1): 创建无边框、透明、置顶窗口；D3D11 swapchain 接入 DirectComposition。
    return false;
}

void WinFloatingWindow::destroy() {
    visible_ = false;
}

bool WinFloatingWindow::is_visible() const {
    return visible_;
}

void WinFloatingWindow::set_visible(bool visible) {
    visible_ = visible;
    // TODO(M1): ShowWindow / HideWindow。
}

void WinFloatingWindow::present(const lb::core::FrameView& frame) {
    (void)frame;
    // TODO(M1): rgba 纹理 -> 合成 -> 提交到 DirectComposition visual。
}

void WinFloatingWindow::set_click_through(bool enabled) {
    click_through_ = enabled;
    // TODO(M1): 切换 WS_EX_TRANSPARENT。
}

void WinFloatingWindow::set_scale(double scale) {
    scale_ = scale;
    // TODO(M1): 更新 visual 缩放矩阵。
}

void WinFloatingWindow::set_opacity(double opacity) {
    opacity_ = opacity;
    // TODO(M1): 更新合成透明度。
}

void WinFloatingWindow::save_geometry() {
    // TODO(M1): 位置与大小写入配置文件（方案 3.4 位置记忆）。
}

void WinFloatingWindow::load_geometry() {
    // TODO(M1): 启动恢复上次位置与大小。
}

} // namespace lb::win