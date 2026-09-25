#pragma once

#include "lb/core/capture_source.h"
#include "lb/core/face_tracker.h"
#include "lb/core/factory.h"
#include "lb/core/model_params.h"
#include "lb/core/semantic_params.h"
#include "lb/core/smoothing.h"
#include "lb/core/solve.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>

namespace lb::core {

struct PipelineConfig {
    std::string      device_id;   // 空 = 默认设备；"__synthetic__" = 合成测试信号
    CaptureConfig    capture;
    InferenceConfig  inference;
    MappingTable     mapping = default_mapping_table();
    SmootherConfig   smoothing;
};

struct PipelineStats {
    double capture_fps = 0;
    double infer_fps = 0;
    double render_fps = 0;
    double latency_ms = 0;
    bool   face_detected = false;
    uint64_t frames = 0;
};

// 应用层管线：采集 -> 推理 -> 求解 -> 平滑 -> 映射。
// 组件经工厂注入（平台无关的编排逻辑），结果帧通过回调送出。
class MotionPipeline {
public:
    using ParamsCallback = std::function<void(const ModelParamList&,
                                              const SemanticParams&,
                                              const PipelineStats&)>;

    MotionPipeline() = default;
    ~MotionPipeline();

    MotionPipeline(const MotionPipeline&) = delete;
    MotionPipeline& operator=(const MotionPipeline&) = delete;

    bool initialize(const PipelineConfig& config);
    bool start(ParamsCallback callback);
    void stop();
    bool is_running() const;
    PipelineStats stats() const;

private:
    void handle_frame(const FrameView& frame);

    PipelineConfig config_;
    std::shared_ptr<ICaptureSource> capture_;
    std::shared_ptr<IFaceTracker> tracker_;
    SemanticSmoother smoother_;
    ParamsCallback callback_;
    std::atomic<bool> running_{false};

    mutable std::mutex stats_mutex_;
    PipelineStats stats_;
    std::chrono::steady_clock::time_point last_capture_ts_{};
    std::chrono::steady_clock::time_point last_infer_ts_{};
};

} // namespace lb::core