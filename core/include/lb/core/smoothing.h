#pragma once

#include "lb/core/semantic_params.h"

namespace lb::core {

// 参数平滑配置（方案 3.2：平滑过渡、无抖动、快速动作轻微延迟补偿）。
struct SmootherConfig {
    double smoothing = 0.6; // 0..1，越小越平滑
    double max_step = 0.35; // 单帧最大变化量，限制快速动作的瞬时跳变
};

// 指数平滑 + 步长上限。快速动作几乎即时响应，缓慢动作被平滑。
class ParamSmoother {
public:
    explicit ParamSmoother(const SmootherConfig& cfg = {});
    float push(float input, double dt_s);
    void reset(float value = 0.f);

private:
    SmootherConfig cfg_;
    float current_ = 0.f;
    bool  has_value_ = false;
};

// 对全部语义通道的平滑器。
class SemanticSmoother {
public:
    explicit SemanticSmoother(const SmootherConfig& cfg = {});
    SemanticParams push(const SemanticParams& input, double dt_s);
    void reset();

private:
    SmootherConfig cfg_;
    ParamSmoother head_yaw_, head_pitch_, head_roll_;
    ParamSmoother eye_open_l_, eye_open_r_, look_x_, look_y_, brow_l_, brow_r_;
    ParamSmoother mouth_open_, mouth_smile_, mouth_press_;
    ParamSmoother torso_tilt_, torso_sway_, neck_pitch_, neck_yaw_, shoulder_shift_;
};

} // namespace lb::core