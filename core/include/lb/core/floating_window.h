#pragma once

#include "lb/core/types.h"

namespace lb::core {

// 桌面浮窗抽象（方案 2.3 的 IFloatingWindow）。
// 无边框、透明背景、始终置顶；可切换点击穿透；位置与大小持久化（方案 3.4）。
class IFloatingWindow {
public:
    virtual ~IFloatingWindow() = default;

    virtual bool create() = 0;
    virtual void destroy() = 0;
    virtual bool is_visible() const = 0;
    virtual void set_visible(bool visible) = 0;

    // 呈现一帧画面，来源格式为 BGRA32 或 RGBA32。
    virtual void present(const FrameView& frame) = 0;

    virtual void set_click_through(bool enabled) = 0;
    virtual void set_scale(double scale) = 0;
    virtual void set_opacity(double opacity) = 0;

    virtual void save_geometry() = 0; // 退出前记忆位置与大小
    virtual void load_geometry() = 0;
};

} // namespace lb::core