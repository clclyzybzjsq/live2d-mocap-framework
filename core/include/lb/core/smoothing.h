#pragma once

#include "lb/core/semantic_params.h"

namespace lb::core {

// 参数平滑配置（方案 3.2：平滑过渡、无抖动、快速动作轻微延迟补偿）。
struct SmootherConfig {
    // 数值越大越平滑（也越迟滞）：alpha = 1 - exp(-(1-smoothing)*dt*60)，
    // 取 0 几乎是原样透传，取 0.9 则滤得很重。
    double smoothing = 0.5;
    // 归一化通道（0..1 / -1..1）的单帧最大变化量
    double max_step = 0.35;
    // 角度通道（单位：度）的单帧最大变化量。
    // 注意：角度通道的量纲是度而不是 0..1，过去统一套用 max_step=0.35 时，
    // 每帧最多只能变 0.35 度 —— 30fps 下转头速度被压到约 10 度/秒，
    // 转头 30 度要三秒。表现就是"动作幅度上不去、响应还慢"。
    double max_step_deg = 30.0;
};

// 指数平滑 + 步长上限。快速动作几乎即时响应，缓慢动作被平滑。
class ParamSmoother {
public:
    explicit ParamSmoother(const SmootherConfig& cfg = {});
    float push(float input, double dt_s);
    void reset(float value = 0.f);
    void set_config(const SmootherConfig& cfg) { cfg_ = cfg; }

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
    // 运行中调整平滑强度（UI 的滑块走这里）
    void set_config(const SmootherConfig& cfg);

private:
    SmootherConfig cfg_;
    ParamSmoother head_yaw_, head_pitch_, head_roll_;
    ParamSmoother eye_open_l_, eye_open_r_, look_x_, look_y_, brow_l_, brow_r_;
    ParamSmoother mouth_open_, mouth_smile_, mouth_press_;
    ParamSmoother torso_tilt_, torso_sway_, neck_pitch_, neck_yaw_, shoulder_shift_;
    ParamSmoother body_position_y_;
};

} // namespace lb::core