#include "lb/core/motion_pipeline.h"

#include <algorithm>

namespace lb::core {

MotionPipeline::~MotionPipeline() {
    stop();
}

bool MotionPipeline::initialize(const PipelineConfig& config) {
    config_ = config;
    capture_ = create_capture_source();
    tracker_ = create_face_tracker();
    if (!capture_ || !tracker_) return false;
    return tracker_->initialize(config_.inference);
}

bool MotionPipeline::start(ParamsCallback callback) {
    if (running_) return false;
    callback_ = std::move(callback);
    smoother_.reset();

    if (!capture_->open(config_.device_id, config_.capture)) {
        return false;
    }
    if (!capture_->start([this](const FrameView& frame) { handle_frame(frame); })) {
        capture_->close();
        return false;
    }

    running_ = true;
    last_capture_ts_ = std::chrono::steady_clock::now();
    last_infer_ts_ = last_capture_ts_;
    return true;
}

void MotionPipeline::stop() {
    running_ = false;
    if (capture_) capture_->stop();
}

bool MotionPipeline::is_running() const {
    return running_;
}

PipelineStats MotionPipeline::stats() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    return stats_;
}

void MotionPipeline::handle_frame(const FrameView& frame) {
    if (!running_ || frame.empty()) return;

    // 采集帧率
    {
        const auto now = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(now - last_capture_ts_).count();
        std::lock_guard<std::mutex> lock(stats_mutex_);
        if (dt > 0.05) {
            const double inst = 1.0 / dt;
            stats_.capture_fps = stats_.capture_fps == 0
                ? inst : stats_.capture_fps * 0.8 + inst * 0.2;
        }
        last_capture_ts_ = now;
    }

    RawFaceFrame raw;
    const auto infer_begin = std::chrono::steady_clock::now();
    if (!tracker_->process(frame, raw)) {
        return;
    }

    // 推理帧率与延迟（帧时间戳到推理完成）
    {
        const auto now = std::chrono::steady_clock::now();
        const double dt = std::chrono::duration<double>(now - last_infer_ts_).count();
        std::lock_guard<std::mutex> lock(stats_mutex_);
        if (dt > 0.05) {
            const double inst = 1.0 / dt;
            stats_.infer_fps = stats_.infer_fps == 0
                ? inst : stats_.infer_fps * 0.8 + inst * 0.2;
            stats_.render_fps = stats_.infer_fps;
        }
        last_infer_ts_ = now;
        stats_.face_detected = raw.face_detected;
        ++stats_.frames;
        if (frame.timestamp_us != 0) {
            const auto now_us = std::chrono::duration_cast<std::chrono::microseconds>(
                now.time_since_epoch()).count();
            const auto frame_us = static_cast<int64_t>(frame.timestamp_us);
            stats_.latency_ms = std::max(0.0, (now_us - frame_us) / 1000.0);
        }
    }

    // 求解 -> 平滑 -> 映射
    SemanticParams semantic = solve_semantics(raw);
    const auto now = std::chrono::steady_clock::now();
    const double dt_s = std::chrono::duration<double>(now - infer_begin).count();
    semantic = smoother_.push(semantic, std::max(dt_s, 0.001));

    const ModelParamList params = apply_mapping(semantic, config_.mapping);
    if (callback_) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        callback_(params, semantic, stats_);
    }
}

} // namespace lb::core