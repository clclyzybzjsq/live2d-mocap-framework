#include "lb/win/win_capture_source.h"

namespace lb::win {

std::vector<lb::core::CaptureDeviceInfo> WinCaptureSource::enumerate_devices() {
    // TODO(M1): Media Foundation 枚举 UVC 设备；首个默认设备 is_default = true。
    return {};
}

bool WinCaptureSource::open(const std::string& device_id, const lb::core::CaptureConfig& config) {
    (void)device_id;
    // TODO(M1): 打开采集会话，按 config 协商分辨率与帧率。
    config_ = config;
    return false;
}

void WinCaptureSource::close() {
    stop();
    config_ = {};
}

bool WinCaptureSource::start(lb::core::FrameCallback on_frame) {
    // TODO(M1): 启动采集线程，逐帧回调 on_frame。
    callback_ = std::move(on_frame);
    running_ = false;
    return false;
}

void WinCaptureSource::stop() {
    running_ = false;
    callback_ = nullptr;
}

bool WinCaptureSource::is_running() const {
    return running_;
}

lb::core::CaptureConfig WinCaptureSource::config() const {
    return config_;
}

} // namespace lb::win