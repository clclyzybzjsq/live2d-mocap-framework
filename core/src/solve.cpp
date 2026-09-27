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

// ARKit 52 路表情系数索引（规范顺序，见 inference/tracker_server.py 的 ARKIT_ORDER）。
// 该顺序由边车按名字重排后保证，两边必须严格一致 —— 一旦错位，某个通道就会读到
// 别的表情上（曾经 index 20 是 jawOpen，实际指到 eyeWideLeft，于是"张嘴"变成了
// "睁大眼睛"；眨眼索引错位则表现为只有一只眼睛跟得上）。
constexpr int kBlendBrowDownLeft = 0;
constexpr int kBlendBrowDownRight = 1;
constexpr int kBlendBrowInnerUp = 2;
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
constexpr int kBlendJawOpen = 25;
constexpr int kBlendMouthPressLeft = 35;
constexpr int kBlendMouthPressRight = 36;
constexpr int kBlendMouthSmileLeft = 43;
constexpr int kBlendMouthSmileRight = 44;

// MediaPipe FaceMesh 眼睑关键点索引
constexpr int kIdxEyeLidL_Outer = 33;
constexpr int kIdxEyeLidL_Inner = 133;
constexpr int kIdxEyeLidL_Up1 = 159;
constexpr int kIdxEyeLidL_Low1 = 145;
constexpr int kIdxEyeLidL_Up2 = 158;
constexpr int kIdxEyeLidL_Low2 = 144;
constexpr int kIdxEyeLidR_Inner = 362;
constexpr int kIdxEyeLidR_Outer = 263;
constexpr int kIdxEyeLidR_Up1 = 386;
constexpr int kIdxEyeLidR_Low1 = 374;
constexpr int kIdxEyeLidR_Up2 = 385;
constexpr int kIdxEyeLidR_Low2 = 380;


float clamp01(float v) { return std::clamp(v, 0.f, 1.f); }
float clamp11(float v) { return std::clamp(v, -1.f, 1.f); }

float at(const std::vector<float>& v, size_t i, size_t dims, size_t k) {
    const size_t idx = i * dims + k;
    return idx < v.size() ? v[idx] : 0.f;
}

constexpr float kPi = 3.14159265f;

// 中性基线的学习速率（每帧向当前值靠近的比例）。
// 取小值：约 2~3 秒的时间常数，足够跟上使用者调整坐姿，
// 又慢到不会把真实的转头/侧身动作吃进基线。
constexpr float kRefLearnRate = 0.012f;

// 无状态时的经验中性值（仅测试工具会用到）
constexpr float kDefaultPitchRatio = 0.40f;

// 眨眼标定。
// MediaPipe 的 eyeBlink 静止时并非严格 0（常有 0.05~0.15 的残留），而自然闭眼
// 也只到 0.7~0.9、到不了 1.0。直接算 1-bs 的结果是：睁眼时始终略带下垂（睁不满），
// 闭眼时留一条缝（闭不严）。这里把"静止值"映射到 open=1、kBlinkFullClose 映射到
// open=0，两个端点因此必然可达。
constexpr float kBlinkFullClose = 0.65f;

// 无状态时用的默认睁眼 EAR（仅测试工具会用到）
constexpr float kDefaultEar = 0.30f;

float blink_closed_ratio(float bs, float& rest, bool& init) {
    // 只在"没在眨"的时候学习基线，否则会把眨眼本身学进基线
    if (bs < 0.35f) {
        if (!init) {
            rest = bs;
            init = true;
        } else {
            rest += 0.02f * (bs - rest);
        }
        rest = std::clamp(rest, 0.0f, 0.3f);
    }
    const float span = std::max(kBlinkFullClose - rest, 0.25f);
    return std::clamp((bs - rest) / span, 0.0f, 1.0f);
}

// 眼睑几何开合度（EAR 的简化版）：上下眼睑间距 / 眼宽。
// 尺度无关；关键是它只反映眼睑本身的位置，视线上下转动时眼睑不动，
// 因此不会像 blendshape 那样"抬头就以为在闭眼"。
float eye_aspect(const std::vector<float>& lm, int c1, int c2, int u1, int l1, int u2, int l2) {
    const float dx = at(lm, c2, 3, 0) - at(lm, c1, 3, 0);
    const float dy = at(lm, c2, 3, 1) - at(lm, c1, 3, 1);
    const float horiz = std::sqrt(dx * dx + dy * dy);
    if (horiz < 1e-5f) return 0.f;
    const float v1 = std::abs(at(lm, l1, 3, 1) - at(lm, u1, 3, 1));
    const float v2 = std::abs(at(lm, l2, 3, 1) - at(lm, u2, 3, 1));
    return (v1 + v2) * 0.5f / horiz;
}

// 几何开合度 -> 睁眼程度。
// 静止值靠自适应基线学习（只在睁眼时更新），闭眼端取静止值的 15%
// （眼睑合拢时 EAR 会掉到静止值的 10~20%），两个端点因此必然可达。
float eye_open_from_aspect(float ear, float& rest, bool& init) {
    if (!init) {
        rest = ear;
        init = true;
    } else if (ear > rest * 0.7f) {
        rest += 0.05f * (ear - rest);
    }
    rest = std::clamp(rest, 0.05f, 0.6f);
    const float closed_level = rest * 0.15f;
    const float span = std::max(rest - closed_level, 0.02f);
    return std::clamp((ear - closed_level) / span, 0.0f, 1.0f);
}

// 学习并返回相对中性的偏移量（首帧只建立基线，输出 0）
float relative_to_ref(float value, float& ref, bool& initialized) {
    if (!initialized) {
        ref = value;
        initialized = true;
        return 0.f;
    }
    ref += kRefLearnRate * (value - ref);
    return value - ref;
}

} // namespace

SemanticParams solve_semantics(const RawFaceFrame& raw, const SolveConfig& cfg,
                               SolveState* state) {
    SemanticParams out;
    out.timestamp_us = raw.timestamp_us;
    out.face_detected = raw.face_detected;
    out.face_confidence = raw.face_confidence;

    const auto& lm = raw.face_landmarks;
    const bool face_ok = raw.face_detected && lm.size() >= 478u * 3u;
    const bool adaptive = (state != nullptr);
    const auto& bs = raw.face_blendshapes;
    const bool bs_ok = raw.face_detected && bs.size() >= 52u;

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

        // 偏航：鼻尖相对脸中线的偏移 / 半脸宽。
        // 关键点实测：转头 30° 时这个比值只有 0.3 左右。过去直接拿它当角度系数
        // 去乘量程，输出只剩几度 —— 这是"动作幅度不够"的首要原因。
        // 现在按 yaw_ratio_span 归一化到满量程。
        const float yaw_ratio = (nose_x - face_cx) / (face_w * 0.5f);
        out.head.yaw = clamp11(yaw_ratio / cfg.yaw_ratio_span) * cfg.max_yaw_deg;

        // 俯仰：鼻尖在"眼线 -> 下巴"这条线上的相对位置。
        // 正脸静止时该比值约 0.35~0.45，因人而异。过去写成
        // (eye_y-nose_y)/face_h*1.2，正脸静止就已经是负值，等于永久低头，
        // 点头那点变化量完全被恒定偏置淹没。改为相对自适应中性基线。
        const float eye_to_chin = std::max(chin_y - eye_y, 1e-4f);
        const float pitch_ratio = (nose_y - eye_y) / eye_to_chin;
        const float pitch_delta = adaptive
            ? relative_to_ref(pitch_ratio, state->pitch_ref, state->pitch_init)
            : (pitch_ratio - kDefaultPitchRatio);
        // 低头 -> 鼻尖离眼线更远 -> 比值变大；Cubism 的 ParamAngleY 正值表示抬头
        out.head.pitch = -clamp11(pitch_delta / cfg.pitch_ratio_span) * cfg.max_pitch_deg;

        // 翻滚：眼线倾角。以"图像中靠左的那只眼"为起点，保证 dx > 0：
        // 角度恒在 ±90° 内（不会像 atan2(dy,dx) 那样在 dx 为负时甩到 ±180°），
        // 同时左右对调后角度取反，因此能正确跟随镜像开关。
        // （之前用 |dx| 把符号信息抹掉了，结果是镜像对它完全无效。）
        {
            const bool l_first = (eye_l_x <= eye_r_x);
            const float edx = l_first ? (eye_r_x - eye_l_x) : (eye_l_x - eye_r_x);
            const float edy = l_first ? (eye_r_y - eye_l_y) : (eye_l_y - eye_r_y);
            out.head.roll = (edx > 1e-5f) ? (std::atan2(edy, edx) * 180.f / kPi) : 0.f;
        }
        out.head.roll = std::clamp(out.head.roll, -cfg.max_roll_deg, cfg.max_roll_deg);
    }

    // ---- 眼睛开合：用眼睑关键点几何，不用 blendshape ----
    // blendshape 的 eyeBlink 是按"眼裂看起来的宽窄"估的，抬头时眼裂在图像上
    // 本来就变窄、系数虚高，于是"一抬头就闭眼"。眼睑几何（上下眼睑间距/眼宽）
    // 与之无关：视线上下转动时眼睑位置几乎不动，只有真正闭眼才归零。
    if (face_ok) {
        const float ear_l = eye_aspect(lm, kIdxEyeLidL_Outer, kIdxEyeLidL_Inner, kIdxEyeLidL_Up1,
                                       kIdxEyeLidL_Low1, kIdxEyeLidL_Up2, kIdxEyeLidL_Low2);
        const float ear_r = eye_aspect(lm, kIdxEyeLidR_Inner, kIdxEyeLidR_Outer, kIdxEyeLidR_Up1,
                                       kIdxEyeLidR_Low1, kIdxEyeLidR_Up2, kIdxEyeLidR_Low2);
        float open_l = adaptive ? eye_open_from_aspect(ear_l, state->ear_rest_l, state->ear_init_l)
                                : std::clamp(ear_l / kDefaultEar, 0.f, 1.f);
        float open_r = adaptive ? eye_open_from_aspect(ear_r, state->ear_rest_r, state->ear_init_r)
                                : std::clamp(ear_r / kDefaultEar, 0.f, 1.f);
        // blendshape 只作为"补足"：已经确实在闭眼时才用它把动作推到完全闭合。
        // 它不能作为触发源（否则抬头/转头就会被误判成眨眼）。
        if (adaptive && bs_ok) {
            if (open_l < 0.8f) {
                const float c =
                    blink_closed_ratio(bs[kBlendEyeBlinkLeft], state->blink_rest_l, state->blink_init_l);
                open_l = std::min(open_l, 1.f - c);
            }
            if (open_r < 0.8f) {
                const float c = blink_closed_ratio(bs[kBlendEyeBlinkRight], state->blink_rest_r,
                                                   state->blink_init_r);
                open_r = std::min(open_r, 1.f - c);
            }
        }
        out.eye.open_left = open_l;
        out.eye.open_right = open_r;
    }

    // ---- 表情（52 路系数直接映射） ----
    if (face_ok && bs_ok) {
        out.eye.brow_left = clamp11(bs[kBlendBrowInnerUp]
                                    + 0.5f * bs[kBlendBrowOuterUpLeft]
                                    - 0.5f * bs[kBlendBrowDownLeft]);
        out.eye.brow_right = clamp11(bs[kBlendBrowInnerUp]
                                     + 0.5f * bs[kBlendBrowOuterUpRight]
                                     - 0.5f * bs[kBlendBrowDownRight]);
        // 视线：向左/右（外/内）与上/下。
        //
        // 这里不随镜像改符号 —— 实测两种镜像状态下都需要同一个符号。
        // 原因是 MediaPipe 的 blendshape 左右命名是按"图像"而不是按"解剖"定的
        // （它按图像中眼睛的位置判断左右），因此镜像后 eyeLookInLeft/Right
        // 本身就会对调，视线已经随镜像翻了；再手动翻一次等于翻回去。
        //
        // 符号方向：eyeLookInLeft 是左眼往鼻侧看（人往自己右边看），而
        // ParamEyeBallX 正值表示瞳孔朝画面右侧，不镜像时人往右看瞳孔在画面上
        // 往左移，故取负号。
        out.eye.look_x = clamp11(bs[kBlendEyeLookOutLeft] - bs[kBlendEyeLookInLeft]);
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
        const float sl_z = at(pl, kIdxPoseShoulderLeft, 3, 2);
        const float sr_x = at(pl, kIdxPoseShoulderRight, 3, 0);
        const float sr_y = at(pl, kIdxPoseShoulderRight, 3, 1);
        const float sr_z = at(pl, kIdxPoseShoulderRight, 3, 2);
        const float hl_z = at(pl, kIdxPoseHipLeft, 3, 2);
        const float hr_z = at(pl, kIdxPoseHipRight, 3, 2);

        const float mid_sx = (sl_x + sr_x) * 0.5f;
        const float mid_sy = (sl_y + sr_y) * 0.5f;
        const float nose_x = at(pl, kIdxPoseNose, 3, 0);
        const float nose_y = at(pl, kIdxPoseNose, 3, 1);

        // 侧倾：肩线倾角（度）。站直时就是 0，是无偏量。
        // 与头部翻滚同理：以图像中靠左的肩为起点，dx 恒为正 ——
        // 既不出现 ±180° 翻转，又能随镜像开关取反。
        // （之前用 |dx| 让它变成了镜像不变量：拨镜像时头转了、身体不动。）
        const bool sl_first = (sl_x <= sr_x);
        const float sdx = sl_first ? (sr_x - sl_x) : (sl_x - sr_x);
        const float sdy = sl_first ? (sr_y - sl_y) : (sl_y - sr_y);
        const float tilt_deg = (sdx > 1e-5f) ? (std::atan2(sdy, sdx) * 180.f / kPi) : 0.f;
        const float tilt_delta = adaptive
            ? relative_to_ref(tilt_deg, state->tilt_ref, state->tilt_init)
            : tilt_deg;
        out.body.torso_sway = clamp11(tilt_delta / cfg.shoulder_tilt_span);

        // 扭转：肩中点相对鼻尖的水平差。转身时肩线带动躯干，这个差值反映躯干
        // 相对头部的扭转；同样是相对量，需减自适应基线。
        const float shift_raw = mid_sx - nose_x;
        const float shift_delta = adaptive
            ? relative_to_ref(shift_raw, state->shift_ref, state->shift_init)
            : shift_raw;
        out.body.shoulder_shift = clamp11(shift_delta / cfg.shoulder_shift_span);

        // 前倾：肩与髋的相对深度。前后倾是三维动作，二维投影里几乎看不出来，
        // 但 pose 关键点带相对深度，可以直接反映前倾/后仰。
        const float sm_z = (sl_z + sr_z) * 0.5f;
        const float hm_z = (hl_z + hr_z) * 0.5f;
        out.body.torso_tilt = clamp11((sm_z - hm_z) * cfg.torso_depth_gain);

        // 上下平动：肩中点的竖直位置相对自适应基线。
        // 摄像头固定时，人蹲下/站起最直接的表现就是肩线整体上下移动，
        // 这是唯一能从单目画面可靠拿到的"身体上下"信号。
        // 图像 y 向下为正，取负号使"人往下"对应模型往下。
        const float pos_delta = adaptive
            ? relative_to_ref(mid_sy, state->pos_ref, state->pos_init)
            : 0.f;
        out.body.position_y = -clamp11(pos_delta / cfg.body_pos_span);

        out.body.neck_pitch = clamp11((mid_sy - nose_y) * 4.f);
        out.body.neck_yaw = clamp11((nose_x - mid_sx) * 4.f);
    }

    return out;
}

} // namespace lb::core