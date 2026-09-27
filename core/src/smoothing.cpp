#include "lb/core/smoothing.h"

#include <algorithm>
#include <cmath>

namespace lb::core {

namespace {
// 角度通道（度）使用独立的单帧步长上限
SmootherConfig degree_config(const SmootherConfig& cfg) {
    SmootherConfig c = cfg;
    c.max_step = cfg.max_step_deg;
    return c;
}

// 躯干通道用更重的平滑，并设一个下限。
// 这些量来自 pose 关键点（肩线），噪声比人脸网格大得多：肩线倾角几度的抖动
// 经满量程放大后在模型上肉眼可见。而躯干本来也不需要像头那样跟手，
// 因此给它独立的平滑下限 —— 即使把"平滑强度"拉到很低，身体也不会抖。
SmootherConfig torso_config(const SmootherConfig& cfg) {
    SmootherConfig c = cfg;
    c.smoothing = std::max(cfg.smoothing, 0.65);
    return c;
}
} // namespace

ParamSmoother::ParamSmoother(const SmootherConfig& cfg) : cfg_(cfg) {}

float ParamSmoother::push(float input, double dt_s) {
    if (!has_value_) {
        current_ = input;
        has_value_ = true;
        return current_;
    }
    // 按帧率归一化的指数平滑系数（以 60 FPS 为基准）。
    const double alpha = 1.0 - std::exp(-(1.0 - cfg_.smoothing) * dt_s * 60.0);
    float next = static_cast<float>(current_ + alpha * (input - current_));
    const float step = next - current_;
    if (std::abs(step) > cfg_.max_step) {
        next = current_ + (step > 0.f ? cfg_.max_step : -cfg_.max_step);
    }
    current_ = next;
    return current_;
}

void ParamSmoother::reset(float value) {
    current_ = value;
    has_value_ = false;
}

SemanticSmoother::SemanticSmoother(const SmootherConfig& cfg)
    : cfg_(cfg),
      head_yaw_(degree_config(cfg)), head_pitch_(degree_config(cfg)),
      head_roll_(degree_config(cfg)),
      eye_open_l_(cfg), eye_open_r_(cfg), look_x_(cfg), look_y_(cfg),
      brow_l_(cfg), brow_r_(cfg),
      mouth_open_(cfg), mouth_smile_(cfg), mouth_press_(cfg),
      torso_tilt_(torso_config(cfg)), torso_sway_(torso_config(cfg)),
      neck_pitch_(torso_config(cfg)), neck_yaw_(torso_config(cfg)),
      shoulder_shift_(torso_config(cfg)), body_position_y_(torso_config(cfg)) {}

SemanticParams SemanticSmoother::push(const SemanticParams& input, double dt_s) {
    SemanticParams out;
    out.head.yaw = head_yaw_.push(input.head.yaw, dt_s);
    out.head.pitch = head_pitch_.push(input.head.pitch, dt_s);
    out.head.roll = head_roll_.push(input.head.roll, dt_s);

    out.eye.open_left = eye_open_l_.push(input.eye.open_left, dt_s);
    out.eye.open_right = eye_open_r_.push(input.eye.open_right, dt_s);
    out.eye.look_x = look_x_.push(input.eye.look_x, dt_s);
    out.eye.look_y = look_y_.push(input.eye.look_y, dt_s);
    out.eye.brow_left = brow_l_.push(input.eye.brow_left, dt_s);
    out.eye.brow_right = brow_r_.push(input.eye.brow_right, dt_s);

    out.mouth.open = mouth_open_.push(input.mouth.open, dt_s);
    out.mouth.smile = mouth_smile_.push(input.mouth.smile, dt_s);
    out.mouth.press = mouth_press_.push(input.mouth.press, dt_s);

    out.body.torso_tilt = torso_tilt_.push(input.body.torso_tilt, dt_s);
    out.body.torso_sway = torso_sway_.push(input.body.torso_sway, dt_s);
    out.body.neck_pitch = neck_pitch_.push(input.body.neck_pitch, dt_s);
    out.body.neck_yaw = neck_yaw_.push(input.body.neck_yaw, dt_s);
    out.body.shoulder_shift = shoulder_shift_.push(input.body.shoulder_shift, dt_s);
    out.body.position_y = body_position_y_.push(input.body.position_y, dt_s);

    out.face_detected = input.face_detected;
    out.face_confidence = input.face_confidence;
    out.timestamp_us = input.timestamp_us;
    return out;
}

void SemanticSmoother::set_config(const SmootherConfig& cfg) {
    cfg_ = cfg;
    const SmootherConfig deg = degree_config(cfg);
    const SmootherConfig torso = torso_config(cfg);
    head_yaw_.set_config(deg);
    head_pitch_.set_config(deg);
    head_roll_.set_config(deg);
    eye_open_l_.set_config(cfg);
    eye_open_r_.set_config(cfg);
    look_x_.set_config(cfg);
    look_y_.set_config(cfg);
    brow_l_.set_config(cfg);
    brow_r_.set_config(cfg);
    mouth_open_.set_config(cfg);
    mouth_smile_.set_config(cfg);
    mouth_press_.set_config(cfg);
    torso_tilt_.set_config(torso);
    torso_sway_.set_config(torso);
    neck_pitch_.set_config(torso);
    neck_yaw_.set_config(torso);
    shoulder_shift_.set_config(torso);
    body_position_y_.set_config(torso);
}

void SemanticSmoother::reset() {
    head_yaw_.reset();
    head_pitch_.reset();
    head_roll_.reset();
    eye_open_l_.reset();
    eye_open_r_.reset();
    look_x_.reset();
    look_y_.reset();
    brow_l_.reset();
    brow_r_.reset();
    mouth_open_.reset();
    mouth_smile_.reset();
    mouth_press_.reset();
    torso_tilt_.reset();
    torso_sway_.reset();
    neck_pitch_.reset();
    neck_yaw_.reset();
    shoulder_shift_.reset();
    body_position_y_.reset();
}

} // namespace lb::core