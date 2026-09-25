#pragma once

#include "lb/core/types.h"

#include <functional>
#include <string>
#include <vector>

namespace lb::core {

struct CaptureConfig {
    int  width = 1280;
    int  height = 720;
    int  fps = 30;
    bool mirror = false;
};

using FrameCallback = std::function<void(const FrameView&)>;

// 采集抽象（方案 2.3 的 ICaptureSource）。
class ICaptureSource {
public:
    virtual ~ICaptureSource() = default;

    virtual std::vector<CaptureDeviceInfo> enumerate_devices() = 0;
    virtual bool open(const std::string& device_id, const CaptureConfig& config) = 0;
    virtual void close() = 0;

    virtual bool start(FrameCallback on_frame) = 0;
    virtual void stop() = 0;
    virtual bool is_running() const = 0;

    virtual CaptureConfig config() const = 0;
};

} // namespace lb::core