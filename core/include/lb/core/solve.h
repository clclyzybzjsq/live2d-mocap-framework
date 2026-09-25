#pragma once

#include "lb/core/face_tracker.h"
#include "lb/core/semantic_params.h"

namespace lb::core {

struct SolveConfig {
    float max_yaw_deg = 18.f;
    float max_pitch_deg = 14.f;
    float max_roll_deg = 20.f;
    bool  use_blendshapes = true;
};

// 求解阶段（纯逻辑，平台无关，方案 3.1）：
// 原始关键点 + 52 路表情系数 -> 语义参数（头部旋转用关键点几何估算，
// 表情系数直接映射；上半身用肩/髋关键点估算）。
SemanticParams solve_semantics(const RawFaceFrame& raw, const SolveConfig& cfg = {});

} // namespace lb::core