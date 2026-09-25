#pragma once

#include <cstddef>
#include <cstdint>

namespace lb::ipc {

// 主进程（C++）与推理边车（Python）共享内存协议。
// 与 inference/protocol.py 保持同步（方案决策：MediaPipe Python 边车）。

constexpr uint32_t kMagic   = 0x4C425F31u; // "LB_1"
constexpr uint32_t kVersion = 2u;

constexpr uint32_t kMaxWidth  = 1280u;
constexpr uint32_t kMaxHeight = 720u;
constexpr uint32_t kMaxPitch  = kMaxWidth * 4u; // BGRA32 最大行字节
constexpr uint32_t kFrameBytes = kMaxPitch * kMaxHeight;
constexpr uint32_t kResultBytes = 64u * 1024u;  // 推理结果缓冲区

constexpr uint32_t kStateIdle     = 0u;
constexpr uint32_t kStateRunning  = 1u;
constexpr uint32_t kStateQuitting = 2u;

#pragma pack(push, 1)
struct SharedHeader {
    uint32_t magic;
    uint32_t version;
    uint32_t state;        // kState*
    uint32_t frame_index;  // 生产者（主进程）写入
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint32_t format;       // lb::core::PixelFormat 数值
    uint64_t timestamp_us;
    uint32_t result_index; // 消费者（边车）写入
    uint32_t result_len;   // 结果字节数
    uint8_t  reserved[80];
};
#pragma pack(pop)

static_assert(sizeof(SharedHeader) == 128, "SharedHeader must be 128 bytes");
static_assert(kFrameBytes == 1280u * 720u * 4u, "kFrameBytes implies 720p BGRA32");

constexpr size_t kFrameOffset  = sizeof(SharedHeader);
constexpr size_t kResultOffset = kFrameOffset + kFrameBytes;
constexpr size_t kSharedTotal  = kResultOffset + kResultBytes;

// 推理结果二进制布局（与 inference/protocol.py 同步）。
#pragma pack(push, 1)
struct SharedResult {
    uint32_t face_landmark_count;  // 关键点数（478 或 0）
    float    face_landmarks[478 * 3];
    uint32_t face_blendshape_count; // 系数个数（52 或 0）
    float    face_blendshapes[52];
    uint32_t pose_landmark_count;   // 关键点数（33 或 0）
    float    pose_landmarks[33 * 3];
    uint8_t  face_detected;
    uint8_t  upper_body_detected;
    float    confidence;
    uint64_t timestamp_us;
};
#pragma pack(pop)

static_assert(sizeof(SharedResult) < kResultBytes, "SharedResult must fit result buffer");

} // namespace lb::ipc