#pragma once

#include "lb/core/face_tracker.h"
#include "lb/core/semantic_params.h"

namespace lb::core {

struct SolveConfig {
    // 头部角度量程。摄像头姿态估算出来的角度普遍偏小，这里的量程决定
    // 关键点比值归一化之后能到多大（见 yaw_ratio_span / pitch_ratio_span）。
    float max_yaw_deg = 30.f;
    float max_pitch_deg = 30.f;
    float max_roll_deg = 25.f;

    // 关键点比值 -> 满量程的标定系数。
    // 鼻尖偏移/半脸宽在转头 30° 时约 0.3，故 0.35 对应满量程；
    // 俯仰用的"眼-鼻 占 眼-下巴"比例变化很小（自然点头只有 0.05 左右），
    // 取 0.18 会只用到三分之一量程，表现为低头抬头幅度不够。
    float yaw_ratio_span = 0.35f;
    float pitch_ratio_span = 0.10f;

    // 躯干：肩线倾角、肩中点与鼻尖的水平差、肩髋相对深度差的标定。
    float shoulder_tilt_span = 12.f;    // 度
    float shoulder_shift_span = 0.12f;  // 归一化图像坐标
    float torso_depth_gain = 4.f;
    // 躯干上下平动：满量程对应的肩中点竖直位移（归一化图像坐标）。
    // 蹲下/站起大约让肩线移动 10~20% 画面高度。
    float body_pos_span = 0.15f;

    bool use_blendshapes = true;
};

// 自适应中性基线。
//
// 人脸个体差异（脸长、鼻梁高度、站位、镜头高低）会让"正脸静止"时的关键点比值
// 并不落在某个固定常数上。若直接减去经验常数，输出会带一个恒定偏置：
// 表现为模型永远歪着/低着一点头，而且真正的动作被偏置淹没。
// 这里用慢速 EMA 学出使用者自己的静止姿态作为中性，输出相对的偏移量。
struct SolveState {
    float pitch_ref = 0.f;
    float tilt_ref = 0.f;
    float shift_ref = 0.f;
    float pos_ref = 0.f;
    bool  pitch_init = false;
    bool  tilt_init = false;
    bool  shift_init = false;
    bool  pos_init = false;
    // 眨眼通道的静止基线（只在没眨眼时学习）
    float blink_rest_l = 0.f;
    float blink_rest_r = 0.f;
    bool  blink_init_l = false;
    bool  blink_init_r = false;
    // 眼睑几何开合度的静止基线（只在睁眼时学习）
    float ear_rest_l = 0.f;
    float ear_rest_r = 0.f;
    bool  ear_init_l = false;
    bool  ear_init_r = false;
};

// 求解阶段（纯逻辑，平台无关，方案 3.1）：
// 原始关键点 + 52 路表情系数 -> 语义参数。
// state 传 nullptr 时不做自适应标定，改用经验中性值（测试工具用）。
SemanticParams solve_semantics(const RawFaceFrame& raw, const SolveConfig& cfg = {},
                               SolveState* state = nullptr);

} // namespace lb::core
