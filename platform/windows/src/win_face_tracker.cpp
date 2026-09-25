#include "lb/win/win_face_tracker.h"

namespace lb::win {

bool WinFaceTracker::initialize(const lb::core::InferenceConfig& config) {
    // TODO(M1): 定位/启动 inference/tracker_server.py（venv 解释器），
    // 建立命名共享内存连接，等待握手完成。
    (void)config;
    initialized_ = false;
    return false;
}

bool WinFaceTracker::process(const lb::core::FrameView& frame, lb::core::RawFaceFrame& out) {
    // TODO(M1): 拷贝帧到共享内存 -> 等待结果环形槽 -> 反序列化 RawFaceFrame。
    (void)frame;
    (void)out;
    return false;
}

} // namespace lb::win