#include "lb/win/win_render_backend.h"

namespace lb::win {

WinRenderBackend::WinRenderBackend(lb::core::IGpuContext& gpu) : gpu_(gpu) {}

bool WinRenderBackend::load_model(const std::string& model_dir) {
    // TODO(M1): Cubism Framework 初始化、加载 model3.json / model.json 目录。
    (void)model_dir;
    return false;
}

void WinRenderBackend::unload_model() {
    has_model_ = false;
}

bool WinRenderBackend::has_model() const {
    return has_model_;
}

std::vector<std::string> WinRenderBackend::parameter_names() const {
    // TODO(M1): 从 CubismModel 枚举参数名（驱动参数面板）。
    return {};
}

void WinRenderBackend::apply_params(const lb::core::ModelParamList& params) {
    // TODO(M1): 写入参数缓存，render 时统一应用到模型。
    (void)params;
}

bool WinRenderBackend::render(uint64_t timestamp_us) {
    // TODO(M1): 设置参数 -> 模型更新 -> 绘制到当前目标纹理。
    (void)timestamp_us;
    return false;
}

} // namespace lb::win