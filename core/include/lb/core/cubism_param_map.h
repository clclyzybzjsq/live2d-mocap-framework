#pragma once

#include <cstddef>

#include "lb/core/semantic_params.h"

namespace lb::core {

// Cubism 标准参数 ID 与取值。ID 为 Live2D Cubism 模型的通用参数名，
// 不同模型可能缺少其中某些项，应用前需按模型实际参数表做存在性检查。
struct CubismParamValue {
    const char* id;
    float value;
};

// 映射结果最大条目数（当前 13 项）
inline constexpr size_t kCubismParamMax = 16;

namespace cubism_param {
inline constexpr const char* kAngleX = "ParamAngleX";
inline constexpr const char* kAngleY = "ParamAngleY";
inline constexpr const char* kAngleZ = "ParamAngleZ";
inline constexpr const char* kEyeLOpen = "ParamEyeLOpen";
inline constexpr const char* kEyeROpen = "ParamEyeROpen";
inline constexpr const char* kEyeBallX = "ParamEyeBallX";
inline constexpr const char* kEyeBallY = "ParamEyeBallY";
inline constexpr const char* kBrowLY = "ParamBrowLY";
inline constexpr const char* kBrowRY = "ParamBrowRY";
inline constexpr const char* kMouthOpenY = "ParamMouthOpenY";
inline constexpr const char* kMouthForm = "ParamMouthForm";
inline constexpr const char* kBodyAngleX = "ParamBodyAngleX";
inline constexpr const char* kBodyAngleZ = "ParamBodyAngleZ";
} // namespace cubism_param

// 语义参数 -> Cubism 标准参数。纯计算，不依赖 Cubism SDK，可离线测试。
//
//   ParamAngleX/Y/Z      head.yaw/pitch/roll      (-30..30 度)
//   ParamEyeLOpen/ROpen  eye.open_left/right      (0..1)
//   ParamEyeBallX/Y      eye.look_x/look_y        (-1..1)
//   ParamBrowLY/RY       eye.brow_left/right      (-1..1)
//   ParamMouthOpenY      mouth.open               (0..1)
//   ParamMouthForm       mouth.smile              (-1..1，0 为无表情)
//   ParamBodyAngleX      body.torso_sway          (-10..10)
//   ParamBodyAngleZ      body.torso_tilt          (-10..10)
//
// 返回写入 out 的条目数；out 为空或容量不足时返回 0。
size_t map_semantics_to_cubism(const SemanticParams& sem, CubismParamValue* out, size_t cap);

} // namespace lb::core
