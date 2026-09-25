#pragma once

#include <cstddef>
#include <cstdint>

namespace lb::ipc {

// 主进程（C++）与推理边车（Python）共享内存协议。
// 与 inference/protocol.py 保持同步（方案决策：MediaPipe Python 边车）。

constexpr uint32_t kMagic   = 0x4C425F31u; // "LB_1"
constexpr uint32_t kVersion = 1u;

constexpr uint32_t kMaxWidth  = 1280u;
constexpr uint32_t kMaxHeight = 720u;
constexpr uint32_t kMaxPitch  = kMaxWidth * 3u; // BGR24 最大行字节
constexpr uint32_t kFrameBytes = kMaxPitch * kMaxHeight;
constexpr uint32_t kResultBytes = 64u * 1024u;  // 推理结果 JSON 缓冲区

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
    uint32_t result_len;   // 结果 JSON 字节数
    uint8_t  reserved[80];
};
#pragma pack(pop)

static_assert(sizeof(SharedHeader) == 128, "SharedHeader must be 128 bytes");
static_assert(kFrameBytes == 1280u * 720u * 3u, "kFrameBytes implies 720p BGR24");

constexpr size_t kFrameOffset  = sizeof(SharedHeader);
constexpr size_t kResultOffset = kFrameOffset + kFrameBytes;
constexpr size_t kSharedTotal  = kResultOffset + kResultBytes;

} // namespace lb::ipc