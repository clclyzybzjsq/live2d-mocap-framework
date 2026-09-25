#include "lb/core/solve.h"

#include <algorithm>
#include <cmath>

namespace lb::core {

namespace {

// MediaPipe FaceMesh 关键点索引（478 点）中与本求解相关的部分。
constexpr int kIdxNoseTip = 1;
constexpr int kIdxEyeLeftOuter = 33;
constexpr int kIdxEyeRightOuter = 263;
constexpr int kIdxChin = 152;
constexpr int kIdxCheekLeft = 234;
constexpr int kIdxCheekRight = 454;

// MediaPipe Pose 上半身关键点（33 点）。
constexpr int kIdxPoseNose = 0;
constexpr int kIdxPoseShoulderLeft = 11;
constexpr int kIdxPoseShoulderRight = 12;
constexpr int kIdxPoseHipLeft = 23;
constexpr int kIdxPoseHipRight = 24;

// ARKit 52 路表情系数索引（与 tracker_server.py 顺序一致）。
constexpr int kBlendBrowInnerUp = 0;
constexpr int kBlendBrowDownLeft = 1;
constexpr int kBlendBrowDownRight = 2;
constexpr int kBlendBrowOuterUpLeft = 3;
constexpr int kBlendBrowOuterUpRight = 4;
constexpr int kBlendEyeBlinkLeft = 8;
constexpr int kBlendEyeBlinkRight = 9;
constexpr int kBlendEyeLookDownLeft = 10;
constexpr int kBlendEyeLookDownRight = 11;
constexpr int kBlendEyeLookInLeft = 12;
constexpr int kBlendEyeLookOutLeft = 14;
constexpr int kBlendEyeLookUpLeft = 16;
constexpr int kBlendEyeLookUpRight = 17;
constexpr int kBlendJawOpen = 20;
constexpr int kBlendMouthPressLeft = 31;
constexpr int kBlendMouthPressRight = 32;
constexpr int kBlendMouthSmileLeft = 39;
constexpr int kBlendMouthSmileRight = 40;

float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }
float clamp11(float v) { return std::clamp(v, -1.f, 1.f); }

float at(const std::vector<float>& v, size_t i, size_t dims, size_t k) {
    const size_t idx = i * dims + k;
    return idx < v.size() ? v[idx] : 0.f;
}

float cross_direction_ratio(float x, float center, float extent) {
    if (extent <= 1e-4f) return 0.f;
    return (x - center) / extent;
}

} // namespace

SemanticParams solve_semantics(const RawFaceFrame& raw, const SolveConfig& cfg) {
    SemanticParams out;
    out.timestamp_us = raw.timestamp_us;
    out.face_detected = raw.face_detected;
    out.face_confidence = raw.face_confidence;

    const auto& lm = raw.face_landmarks;
    const bool face_ok = raw.face_detected && lm.size() >= 478u * 3u;

    // ---- 头部（关键点几何估算） ----
    if (face_ok) {
        const float lx = at(lm, kIdxCheekLeft, 3, 0);
        const float rx = at(lm, kIdxCheekRight, 3, 0);
        const float face_w = std::max(rx - lx, 1e-4f);
        const float face_cx = (lx + rx) * 0.5f;

        const float nose_x = at(lm, kIdxNoseTip, 3, 0);
        const float nose_y = at(lm, kIdxNoseTip, 3, 1);

        const float eye_l_x = at(lm, kIdxEyeLeftOuter, 3, 0);
        const float eye_l_y = at(lm, kIdxEyeLeftOuter, 3, 1);
        const float eye_r_x = at(lm, kIdxEyeRightOuter, 3, 0);
        const float eye_r_y = at(lm, kIdxEyeRightOuter, 3, 1);
        const float eye_y = (eye_l_y + eye_r_y) * 0.5f;
        const float chin_y = at(lm, kIdxChin, 3, 1);
        const float face_h = std::max(chin_y - eye_y, 1e-4f);

        out.head.yaw = clamp11(cross_direction_ratio(nose_x, face_cx, face_w * 0.5f))
                       * cfg.max_yaw_deg;
        out.head.pitch = clamp11((eye_y - nose_y) / face_h * 1.2f) * cfg.max_pitch_deg;
        out.head.roll = std::atan2(eye_r_y - eye_l_y, eye_r_x - eye_l_x)
                        * 180.f / 3.14159265f;
        out.head.roll = std::clamp(out.head.roll, -cfg.max_roll_deg, cfg.max_roll_deg);
    }

    // ---- 表情（52 路系数直接映射） ----
    const auto& bs = raw.face_blendshapes;
    const bool bs_ok = raw.face_detected && bs.size() >= 52u;
    if (face_ok && bs_ok) {
        out.eye.open_left = 1.f - clamp01(bs[kBlendEyeBlinkLeft]);
        out.eye.open_right = 1.f - clamp01(bs[kBlendEyeBlinkRight]);
        out.eye.brow_left = clamp11(bs[kBlendBrowInnerUp]
                                    + 0.5f * bs[kBlendBrowOuterUpLeft]
                                    - 0.5f * bs[kBlendBrowDownLeft]);
        out.eye.brow_right = clamp11(bs[kBlendBrowInnerUp]
                                     + 0.5f * bs[kBlendBrowOuterUpRight]
                                     - 0.5f * bs[kBlendBrowDownRight]);
        // 视线：向左/右（外/内）与上/下
        out.eye.look_x = clamp11(bs[kBlendEyeLookInLeft] - bs[kBlendEyeLookOutLeft]);
        out.eye.look_y = clamp11(bs[kBlendEyeLookUpLeft] - bs[kBlendEyeLookDownLeft]);

        out.mouth.open = clamp01(bs[kBlendJawOpen]);
        out.mouth.smile = std::max(bs[kBlendMouthSmileLeft], bs[kBlendMouthSmileRight]);
        out.mouth.press = std::max(bs[kBlendMouthPressLeft], bs[kBlendMouthPressRight]);
    }

    // ---- 上半身（pose 关键点估算） ----
    const auto& pl = raw.pose_landmarks;
    if (raw.upper_body_detected && pl.size() >= 33u * 3u) {
        const float sl_x = at(pl, kIdxPoseShoulderLeft, 3, 0);
        const float sl_y = at(pl, kIdxPoseShoulderLeft, 3, 1);
        const float sr_x = at(pl, kIdxPoseShoulderRight, 3, 0);
        const float sr_y = at(pl, kIdxPoseShoulderRight, 3, 1);
        const float hl_y = at(pl, kIdxPoseHipLeft, 3, 1);
        const float hr_y = at(pl, kIdxPoseHipRight, 3, 1);

        const float mid_sx = (sl_x + sr_x) * 0.5f;
        const float mid_sy = (sl_y + sr_y) * 0.5f;
        const float mid_hy = (hl_y + hr_y) * 0.5f;

        out.body.torso_tilt = clamp11((mid_hy - mid_sy) * 2.f);
        out.body.torso_sway = clamp11((mid_sx - 0.5f) * 2.f);
        out.body.shoulder_shift = out.body.torso_sway;

        const float nose_x = at(pl, kIdxPoseNose, 3, 0);
        const float nose_y = at(pl, kIdxPoseNose, 3, 1);
        out.body.neck_pitch = clamp11((mid_sy - nose_y) * 2.f);
        out.body.neck_yaw = clamp11((nose_x - mid_sx) * 2.f);
    }

    return out;
}

} // namespace lb::core