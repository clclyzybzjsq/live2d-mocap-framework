#pragma once

#include "lb/core/capture_source.h"

#include <atomic>
#include <memory>
#include <thread>

namespace lb::win {

// Windows 采集实现：Media Foundation SourceReader（UVC 摄像头），
// 输出 BGRA32 帧；无摄像头时可用合成测试信号（synthetic_device_id）。
class WinCaptureSource final : public lb::core::ICaptureSource {
public:
    WinCaptureSource();
    ~WinCaptureSource() override;

    static std::string synthetic_device_id() { return "__synthetic__"; }

    std::vector<lb::core::CaptureDeviceInfo> enumerate_devices() override;
    bool open(const std::string& device_id, const lb::core::CaptureConfig& config) override;
    void close() override;
    bool start(lb::core::FrameCallback on_frame) override;
    void stop() override;
    bool is_running() const override;
    lb::core::CaptureConfig config() const override;

private:
    struct Impl;
    void worker_loop();

    std::unique_ptr<Impl> impl_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    bool synthetic_ = false;
    lb::core::CaptureConfig config_;
    lb::core::FrameCallback callback_;
};

} // namespace lb::win