#pragma once

#include "lb/core/floating_window.h"

namespace lb::win {

// Windows 浮窗实现。TODO(M1)：D3D11 + DirectComposition 逐像素透明窗口，
// WS_EX_TRANSPARENT 点击穿透；几何持久化（方案 3.4）。当前为桩实现。
class WinFloatingWindow final : public lb::core::IFloatingWindow {
public:
    WinFloatingWindow() = default;
    ~WinFloatingWindow() override = default;

    bool create() override;
    void destroy() override;
    bool is_visible() const override;
    void set_visible(bool visible) override;

    void present(const lb::core::FrameView& frame) override;

    void set_click_through(bool enabled) override;
    void set_scale(double scale) override;
    void set_opacity(double opacity) override;

    void save_geometry() override;
    void load_geometry() override;

private:
    bool visible_ = false;
    bool click_through_ = false;
    double scale_ = 1.0;
    double opacity_ = 1.0;
};

} // namespace lb::win