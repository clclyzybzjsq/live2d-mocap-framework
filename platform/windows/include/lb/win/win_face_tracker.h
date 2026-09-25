#pragma once

#include "lb/core/face_tracker.h"

#include <string>

namespace lb::win {

// MediaPipe Python 边车的共享内存客户端（方案决策：免 MSVC）。
// 工作方式：initialize 时创建命名共享内存并拉起边车进程，
// process 写入采集帧、轮询二进制结果（协议见 lb/core/ipc_protocol.h）。
class WinFaceTracker final : public lb::core::IFaceTracker {
public:
    WinFaceTracker() = default;
    ~WinFaceTracker() override;

    bool initialize(const lb::core::InferenceConfig& config) override;
    bool process(const lb::core::FrameView& frame, lb::core::RawFaceFrame& out) override;
    void shutdown() override;

private:
    bool initialized_ = false;
    void* map_handle_ = nullptr; // HANDLE
    void* map_view_ = nullptr;   // 映射视图
    void* child_ = nullptr;      // 边车进程句柄
    std::string shm_name_ = "lb_capture_frames";
};

} // namespace lb::win