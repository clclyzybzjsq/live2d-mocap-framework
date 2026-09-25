#pragma once

#include "lb/core/model_params.h"

#include <cstdint>
#include <string>
#include <vector>

namespace lb::core {

// 渲染后端抽象（方案 2.3 的 IRenderBackend）。
// 宿主为 Live2D Cubism SDK；实现位于 GPU 后端之上。
class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;

    virtual bool load_model(const std::string& model_dir) = 0;
    virtual void unload_model() = 0;
    virtual bool has_model() const = 0;

    virtual std::vector<std::string> parameter_names() const = 0;
    virtual void apply_params(const ModelParamList& params) = 0;

    // 渲染到当前绑定的目标；失败返回 false。
    virtual bool render(uint64_t timestamp_us) = 0;
};

} // namespace lb::core