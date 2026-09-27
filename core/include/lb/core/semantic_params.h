#pragma once

#include <cstdint>

namespace lb::core {

// 与平台、模型无关的语义参数集（方案 3.1）。
// 求解阶段：原始关键点 -> SemanticParams；映射阶段：-> Live2D 参数。

struct HeadPose {
    float yaw = 0.f;    // 度
    float pitch = 0.f;
    float roll = 0.f;
};

struct EyeState {
    float open_left = 1.f;
    float open_right = 1.f;
    float look_x = 0.f;
    float look_y = 0.f;
    float brow_left = 0.f;
    float brow_right = 0.f;
};

struct MouthState {
    float open = 0.f;
    float smile = 0.f;
    float press = 0.f;
};

struct BodyPose {
    float torso_tilt = 0.f;   // 躯干前倾
    float torso_sway = 0.f;   // 躯干侧倾
    float neck_pitch = 0.f;
    float neck_yaw = 0.f;
    float shoulder_shift = 0.f;
    // 躯干上下平动：肩中点竖直位置相对自适应基线的偏移（-1 下 .. +1 上）
    float position_y = 0.f;
};

struct SemanticParams {
    HeadPose   head;
    EyeState   eye;
    MouthState mouth;
    BodyPose   body;

    bool   face_detected = false;
    float  face_confidence = 0.f;
    uint64_t timestamp_us = 0;

    void reset() {
        head = {};
        eye = {};
        mouth = {};
        body = {};
        face_detected = false;
        face_confidence = 0.f;
        timestamp_us = 0;
    }
};

} // namespace lb::core