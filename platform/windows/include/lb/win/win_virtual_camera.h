#pragma once

#include <windows.h>
#include <atomic>
#include <mutex>

#include "lb/core/virtual_camera.h"

namespace lb::win {

// Windows 虚拟摄像头实现：DirectShow 用户态 filter（lb_vcam_filter.dll）+
// 共享内存（lb_vcam_frames）喂帧。首次运行自动注册（HKCU 用户态）、
// 每次启动自愈重注册（方案 4.0 零配置）。
class WinVirtualCamera final : public lb::core::IVirtualCamera {
public:
    WinVirtualCamera() = default;
    ~WinVirtualCamera() override { stop(); }

    bool is_registered() const override;
    bool ensure_registered() override;

    bool start(const lb::core::VirtualCameraFormat& format) override;
    bool push_frame(const lb::core::FrameView& frame) override;
    void stop() override;

    bool is_running() const override;
    bool has_consumer() const override;

    // 测试/卸载辅助：调用 filter 的 DllUnregisterServer
    void unregister_device();

private:
    lb::core::VirtualCameraFormat format_;
    std::atomic<bool> running_{false};
    HANDLE map_ = nullptr;
    void* view_ = nullptr;
    mutable std::mutex mutex_;
};

} // namespace lb::win