#pragma once

#include "lb/core/types.h"

namespace lb::core {

struct VirtualCameraFormat {
    int         width = 1920;
    int         height = 1080;
    int         fps = 30;
    PixelFormat preferred_format = PixelFormat::YUY2; // 协商起始格式
};

// 虚拟摄像头抽象（方案 2.3 的 IVirtualCamera）。
// 零配置要求见方案 4.0：设备在首次运行时自动注册，后续启动自愈，
// 界面不出现任何安装/配置流程。
class IVirtualCamera {
public:
    virtual ~IVirtualCamera() = default;

    virtual bool is_registered() const = 0;
    virtual bool ensure_registered() = 0;

    virtual bool start(const VirtualCameraFormat& format) = 0;
    virtual bool push_frame(const FrameView& frame) = 0;
    virtual void stop() = 0;

    virtual bool is_running() const = 0;
    virtual bool has_consumer() const = 0;
};

} // namespace lb::core