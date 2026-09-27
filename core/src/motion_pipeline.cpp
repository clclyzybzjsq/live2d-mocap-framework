#include "lb/core/motion_pipeline.h"

#include <algorithm>

namespace lb::core {

MotionPipeline::~MotionPipeline() {
    stop();
}

bool MotionPipeline::initialize(const PipelineConfig& config) {
    config_ = config;
    // 平滑配置必须真正落到平滑器上：此前只存了 config_，平滑器一直是默认构造的，
    // 于是 UI 上的平滑强度滑块（以及 PipelineConfig::smoothing）全都被静默忽略
    smoother_.set_config(config_.smoothing);
    pending_smoothing_.store(config_.smoothing.smoothing);
    capture_ = create_capture_source();
    tracker_ = create_face_tracker();
    if (!capture_ || !tracker_) return false;
    return tracker_->initialize(config_.inference);
}

void MotionPipeline::set_smoothing(double smoothing) {
    // 平滑器只在采集线程里使用，这里只存下请求值，由采集线程在下一帧取用
    pending_smoothing_.store(std::clamp(smoothing, 0.0, 0.95));
}

void MotionPipeline::set_mirror(bool enabled) {
    config_.capture.mirror = enabled;
    // 视线不需要在这里处理：MediaPipe 的 blendshape 左右命名是按图像的，
    // 画面翻转后它自己就会对调（详见 solve.cpp 里视线的注释）
    if (capture_) capture_->set_mirror(enabled);
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
    SemanticParams semantic = solve_semantics(raw, solve_cfg_, &solve_state_);
    // 平滑强度可能在运行中被 UI 调整，这里取一次（采集线程独占平滑器）
    const double want = pending_smoothing_.load();
    if (want != config_.smoothing.smoothing) {
        config_.smoothing.smoothing = want;
        smoother_.set_config(config_.smoothing);
    }
    // 平滑的时间常数必须用"距上一帧的间隔"，不是本次推理耗时：
    // 推理快而出帧慢时用推理耗时会算出过小的 dt，导致过度平滑（动作迟滞）。
    const auto now = std::chrono::steady_clock::now();
    double dt_s = 0.033;
    if (last_solve_ts_.time_since_epoch().count() != 0) {
        dt_s = std::chrono::duration<double>(now - last_solve_ts_).count();
    }
    last_solve_ts_ = now;
    if (dt_s < 0.005) dt_s = 0.005;
    if (dt_s > 0.2) dt_s = 0.2;
    semantic = smoother_.push(semantic, dt_s);

    const ModelParamList params = apply_mapping(semantic, config_.mapping);
    if (callback_) {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        callback_(frame, params, semantic, stats_);
    }
}

} // namespace lb::core