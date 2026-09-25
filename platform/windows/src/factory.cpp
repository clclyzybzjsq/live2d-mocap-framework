#include "lb/core/factory.h"

#include "lb/win/win_capture_source.h"
#include "lb/win/win_virtual_camera.h"
#include "lb/win/win_floating_window.h"
#include "lb/win/win_gpu_context.h"
#include "lb/win/win_render_backend.h"
#include "lb/win/win_face_tracker.h"

namespace lb::core {

std::shared_ptr<ICaptureSource> create_capture_source() {
    return std::make_shared<lb::win::WinCaptureSource>();
}

std::shared_ptr<IVirtualCamera> create_virtual_camera() {
    return std::make_shared<lb::win::WinVirtualCamera>();
}

std::shared_ptr<IFloatingWindow> create_floating_window() {
    return std::make_shared<lb::win::WinFloatingWindow>();
}

std::shared_ptr<IGpuContext> create_gpu_context() {
    return std::make_shared<lb::win::WinGpuContext>();
}

std::shared_ptr<IRenderBackend> create_render_backend(IGpuContext& gpu) {
    return std::make_shared<lb::win::WinRenderBackend>(gpu);
}

std::shared_ptr<IFaceTracker> create_face_tracker() {
    return std::make_shared<lb::win::WinFaceTracker>();
}

} // namespace lb::core