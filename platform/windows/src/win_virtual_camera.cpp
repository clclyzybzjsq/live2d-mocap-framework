#include "lb/win/win_virtual_camera.h"

namespace lb::win {

bool WinVirtualCamera::is_registered() const {
    // TODO(M2): 枚举 DirectShow 视频设备，检查本应用设备是否存在。
    return false;
}

bool WinVirtualCamera::ensure_registered() {
    // TODO(M2): 首次运行复制 filter DLL 并调用 DllRegisterServer（HKCU 用户态）；
    // 每次启动自愈重注册。全程无 UI 交互（方案 4.0）。
    return false;
}

bool WinVirtualCamera::start(const lb::core::VirtualCameraFormat& format) {
    // TODO(M2): 启动共享内存映射 + filter 消费线程。
    format_ = format;
    running_ = false;
    return false;
}

bool WinVirtualCamera::push_frame(const lb::core::FrameView& frame) {
    // TODO(M2): 写入共享内存环形缓冲；无消费者时直接丢弃。
    (void)frame;
    return false;
}

void WinVirtualCamera::stop() {
    running_ = false;
}

bool WinVirtualCamera::is_running() const {
    return running_;
}

bool WinVirtualCamera::has_consumer() const {
    // TODO(M2): 查询 filter 是否被打开（是否有应用在读取）。
    return false;
}

} // namespace lb::win