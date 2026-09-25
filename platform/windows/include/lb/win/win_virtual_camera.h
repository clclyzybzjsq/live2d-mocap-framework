#pragma once

#include "lb/core/virtual_camera.h"

namespace lb::win {

// Windows 虚拟摄像头实现。TODO(M2)：DirectShow 用户态 filter +
// 共享内存喂帧；首次运行自动注册、每次启动自愈（方案 4.0 零配置）。
// 当前为桩实现，接口形状已定。
class WinVirtualCamera final : public lb::core::IVirtualCamera {
public:
    WinVirtualCamera() = default;
    ~WinVirtualCamera() override = default;

    bool is_registered() const override;
    bool ensure_registered() override;

    bool start(const lb::core::VirtualCameraFormat& format) override;
    bool push_frame(const lb::core::FrameView& frame) override;
    void stop() override;

    bool is_running() const override;
    bool has_consumer() const override;

private:
    lb::core::VirtualCameraFormat format_;
    bool running_ = false;
};

} // namespace lb::win