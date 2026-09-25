#pragma once

#include <memory>

namespace lb::core {

class ICaptureSource;
class IVirtualCamera;
class IFloatingWindow;
class IGpuContext;
class IRenderBackend;
class IFaceTracker;

// 各抽象对象的平台工厂。实现位于平台适配层（Windows 为 lb_win）。
std::shared_ptr<ICaptureSource> create_capture_source();
std::shared_ptr<IVirtualCamera> create_virtual_camera();
std::shared_ptr<IFloatingWindow> create_floating_window();
std::shared_ptr<IGpuContext> create_gpu_context();
std::shared_ptr<IRenderBackend> create_render_backend(IGpuContext& gpu);
std::shared_ptr<IFaceTracker> create_face_tracker();

} // namespace lb::core