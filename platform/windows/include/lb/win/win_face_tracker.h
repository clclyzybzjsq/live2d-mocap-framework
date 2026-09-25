#pragma once

#include "lb/core/face_tracker.h"

#include <string>

namespace lb::win {

// 推理客户端。TODO(M1)：作为 Python 边车（inference/tracker_server.py）的
// 共享内存客户端：写入采集帧、读取 RawFaceFrame。当前为桩实现。
class WinFaceTracker final : public lb::core::IFaceTracker {
public:
    WinFaceTracker() = default;
    ~WinFaceTracker() override = default;

    bool initialize(const lb::core::InferenceConfig& config) override;
    bool process(const lb::core::FrameView& frame, lb::core::RawFaceFrame& out) override;

private:
    bool initialized_ = false;
};

} // namespace lb::win