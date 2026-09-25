#pragma once

#include "lb/core/gpu_context.h"
#include "lb/core/render_backend.h"

namespace lb::win {

// Live2D 渲染后端。TODO(M1)：接入 Cubism SDK for Native（D3D11 后端），
// 加载模型、写入参数、渲染到 GPU 纹理。当前为桩实现。
class WinRenderBackend final : public lb::core::IRenderBackend {
public:
    explicit WinRenderBackend(lb::core::IGpuContext& gpu);
    ~WinRenderBackend() override = default;

    bool load_model(const std::string& model_dir) override;
    void unload_model() override;
    bool has_model() const override;

    std::vector<std::string> parameter_names() const override;
    void apply_params(const lb::core::ModelParamList& params) override;

    bool render(uint64_t timestamp_us) override;

private:
    lb::core::IGpuContext& gpu_;
    bool has_model_ = false;
};

} // namespace lb::win