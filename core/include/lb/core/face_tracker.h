#pragma once

#include "lb/core/types.h"

#include <string>
#include <vector>

namespace lb::core {

struct InferenceConfig {
    std::string model_dir; // MediaPipe task 文件所在目录
    bool use_gpu = true;
    int  max_faces = 8;    // 多人场景取最大脸（方案 3.1）
};

// 推理模块原始输出。求解阶段（纯逻辑）在此基础上产出语义参数。
struct RawFaceFrame {
    std::vector<float> face_landmarks;   // 478 关键点，x,y,z 三元组
    std::vector<float> face_blendshapes; // 52 路 ARKit 风格表情系数
    std::vector<float> pose_landmarks;   // 上半身关键点

    bool   face_detected = false;
    float  face_confidence = 0.f;
    bool   upper_body_detected = false;
    uint64_t timestamp_us = 0;
};

// 推理抽象。Windows 实现为 Python 边车客户端
// （MediaPipe 原生 C++ 仅支持 MSVC，本工程使用共享内存边车，见方案决策）。
class IFaceTracker {
public:
    virtual ~IFaceTracker() = default;

    virtual bool initialize(const InferenceConfig& config) = 0;
    virtual bool process(const FrameView& frame, RawFaceFrame& out) = 0;

    // 释放推理资源（结束边车进程等）。
    virtual void shutdown() {}
};

} // namespace lb::core