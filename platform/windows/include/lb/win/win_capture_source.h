#pragma once

#include "lb/core/capture_source.h"

namespace lb::win {

// Windows 采集实现。TODO(M1)：Media Foundation 采集图（UVC 摄像头），
// 帧回调推 FrameView。当前为桩实现，接口形状已定。
class WinCaptureSource final : public lb::core::ICaptureSource {
public:
    WinCaptureSource() = default;
    ~WinCaptureSource() override = default;

    std::vector<lb::core::CaptureDeviceInfo> enumerate_devices() override;
    bool open(const std::string& device_id, const lb::core::CaptureConfig& config) override;
    void close() override;

    bool start(lb::core::FrameCallback on_frame) override;
    void stop() override;
    bool is_running() const override;

    lb::core::CaptureConfig config() const override;

private:
    lb::core::CaptureConfig config_;
    lb::core::FrameCallback callback_;
    bool running_ = false;
};

} // namespace lb::win