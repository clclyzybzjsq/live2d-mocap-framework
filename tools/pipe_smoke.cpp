// M1 管线冒烟测试：直接驱动 采集 -> 推理边车 -> 求解 -> 映射 全链路。
// 用法：
//   lb_smoke [--device <id|synthetic>] [--frames 240]
// 环境变量：
//   LB_MODEL_DIR      模型目录（默认 inference/models）
//   LB_SIDECAR_PYTHON 边车解释器（默认 D:/deps/venv/Scripts/python.exe）
#include "lb/core/factory.h"
#include "lb/core/motion_pipeline.h"
#include "lb/core/solve.h"
#include "lb/win/win_capture_source.h"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

std::string env_or(const char* key, const std::string& fallback) {
    const char* v = std::getenv(key);
    return (v && *v) ? std::string(v) : fallback;
}

} // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0); // 无缓冲：崩溃时保留已打印信息
    std::string device;
    int max_frames = 240;
    int width = 640;
    int height = 480;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--device" && i + 1 < argc) device = argv[++i];
        else if (arg == "--frames" && i + 1 < argc) max_frames = std::atoi(argv[++i]);
        else if (arg == "--width" && i + 1 < argc) width = std::atoi(argv[++i]);
        else if (arg == "--height" && i + 1 < argc) height = std::atoi(argv[++i]);
    }

    if (device.empty() || device == "synthetic") {
        device = lb::win::WinCaptureSource::synthetic_device_id();
    }

    auto capture = lb::core::create_capture_source();
    auto tracker = lb::core::create_face_tracker();
    if (!capture || !tracker) {
        std::printf("FAIL: factories returned null\n");
        return 1;
    }

    const auto devices = capture->enumerate_devices();
    std::printf("cameras: %zu\n", devices.size());
    for (const auto& d : devices) std::printf("  - %s\n", d.name.c_str());

    if (device == "camera") {
        if (devices.empty()) {
            std::printf("FAIL: no camera found\n");
            return 1;
        }
        device = devices.front().id;
    }

    lb::core::InferenceConfig infer;
    infer.model_dir = env_or("LB_MODEL_DIR", "D:/ournotes_vtube/inference/models");
    if (!tracker->initialize(infer)) {
        std::printf("FAIL: tracker init\n");
        return 1;
    }

    lb::core::CaptureConfig cap;
    cap.width = width;
    cap.height = height;
    cap.fps = 30;

    bool opened = capture->open(device, cap);
    if (!opened) {
        std::printf("FAIL: capture open '%s'\n", device.c_str());
        return 1;
    }

    long frames = 0;
    long detected = 0;
    double last_inst_ms = 0.0;
    bool printed_header = false;
    std::chrono::steady_clock::time_point window_start =
        std::chrono::steady_clock::now();
    long window_frames_base = 0;

    const bool ok = capture->start([&](const lb::core::FrameView& frame) {
        lb::core::RawFaceFrame raw;
        const auto t0 = std::chrono::steady_clock::now();
        if (!tracker->process(frame, raw)) return;
        const auto t1 = std::chrono::steady_clock::now();
        last_inst_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();

        const auto sem = lb::core::solve_semantics(raw);
        ++frames;
        if (sem.face_detected) ++detected;
        if (!printed_header) {
            std::printf("frame face yaw pitch roll eyeL mouth inst_ms fps\n");
            printed_header = true;
        }
        if (frames % 30 == 1) {
            const double window_s = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - window_start).count();
            const double fps = (frames - window_frames_base) / std::max(window_s, 1e-4);
            std::printf("%ld %s %+.1f %+.1f %+.1f %.2f %.2f %.1f %.1f\n",
                        frames, sem.face_detected ? "yes" : "no ",
                        sem.head.yaw, sem.head.pitch, sem.head.roll,
                        sem.eye.open_left, sem.mouth.open,
                        last_inst_ms, fps);
            window_start = std::chrono::steady_clock::now();
            window_frames_base = frames;
            std::fflush(stdout);
        }
    });

    if (!ok) {
        std::printf("FAIL: capture start\n");
        return 1;
    }

    // 等足帧数或超时
    const std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (frames < max_frames && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    capture->stop();
    tracker->shutdown();

    std::printf("RESULT frames=%ld detected=%ld last_inst=%.1fms\n",
                frames, detected, last_inst_ms);
    if (frames == 0) {
        std::printf("FAIL: no frames processed\n");
        return 1;
    }
    if (detected == 0) {
        std::printf("WARN: face not detected (camera may not see a person)\n");
    }
    std::printf("OK\n");
    return 0;
}