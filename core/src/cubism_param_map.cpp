#include "lb/core/cubism_param_map.h"

#include <algorithm>

namespace lb::core {

namespace {
float clampf(float v, float lo, float hi) { return std::clamp(v, lo, hi); }
} // namespace

size_t map_semantics_to_cubism(const SemanticParams& sem, CubismParamValue* out, size_t cap) {
    if (!out || cap == 0) return 0;

    size_t n = 0;
    const auto put = [&](const char* id, float v) {
        if (n < cap) out[n++] = CubismParamValue{id, v};
    };

    // 头部朝向：语义参数已是角度
    put(cubism_param::kAngleX, clampf(sem.head.yaw, -30.f, 30.f));
    put(cubism_param::kAngleY, clampf(sem.head.pitch, -30.f, 30.f));
    put(cubism_param::kAngleZ, clampf(sem.head.roll, -30.f, 30.f));

    // 眼（开合、视线、眉）
    put(cubism_param::kEyeLOpen, clampf(sem.eye.open_left, 0.f, 1.f));
    put(cubism_param::kEyeROpen, clampf(sem.eye.open_right, 0.f, 1.f));
    put(cubism_param::kEyeBallX, clampf(sem.eye.look_x, -1.f, 1.f));
    put(cubism_param::kEyeBallY, clampf(sem.eye.look_y, -1.f, 1.f));
    put(cubism_param::kBrowLY, clampf(sem.eye.brow_left, -1.f, 1.f));
    put(cubism_param::kBrowRY, clampf(sem.eye.brow_right, -1.f, 1.f));

    // 口
    put(cubism_param::kMouthOpenY, clampf(sem.mouth.open, 0.f, 1.f));
    put(cubism_param::kMouthForm, clampf(sem.mouth.smile, -1.f, 1.f));

    // 身体（Cubism 模板通常给身体摆动较小的范围）
    put(cubism_param::kBodyAngleX, clampf(sem.body.torso_sway, -10.f, 10.f));
    put(cubism_param::kBodyAngleZ, clampf(sem.body.torso_tilt, -10.f, 10.f));

    return n;
}

} // namespace lb::core
