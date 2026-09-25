#include "lb/core/smoothing.h"

#include <algorithm>
#include <cmath>

namespace lb::core {

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
      head_yaw_(cfg), head_pitch_(cfg), head_roll_(cfg),
      eye_open_l_(cfg), eye_open_r_(cfg), look_x_(cfg), look_y_(cfg),
      brow_l_(cfg), brow_r_(cfg),
      mouth_open_(cfg), mouth_smile_(cfg), mouth_press_(cfg),
      torso_tilt_(cfg), torso_sway_(cfg), neck_pitch_(cfg), neck_yaw_(cfg),
      shoulder_shift_(cfg) {}

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

    out.face_detected = input.face_detected;
    out.face_confidence = input.face_confidence;
    out.timestamp_us = input.timestamp_us;
    return out;
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
}

} // namespace lb::core