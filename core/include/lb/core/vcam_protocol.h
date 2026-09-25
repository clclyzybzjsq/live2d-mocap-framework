#pragma once

#include <cstdint>
#include <cstddef>

// 虚拟摄像头共享内存协议（写端=应用进程，读端=DirectShow filter 消费进程）。
// 与捕获 IPC 同风格：固定头 + 帧数据，无 JSON、无队列，消费者按 frame_index 取最新帧。
//
// 存储格式固定为 YUY2（写端负责从 BGRA 转换），消费端直接复制推进，
// 避免在 OBS/Teams/浏览器等消费进程中做像素转换。
//
// 共享内存 tag:  L"lb_vcam_frames"
// 映射大小:      sizeof(SharedHeaderVcam) + kVcamBytes（= 1280*720*2）
// 多进程写安全: frame_index 与像素在一次 MemBarrier 内发布（见 header_write_frame）。

namespace lb::core {

static constexpr uint32_t kVcamMagic    = 0x4C425643u; // "LBVC"
static constexpr uint32_t kVcamVersion  = 1u;

// 状态机（由写端驱动；读端只观察 frame_index 与 consumer_count）
enum VcamState : uint32_t {
    kVcamIdle    = 0,
    kVcamRunning = 1,
};

// 缓冲上限：720p YUY2（该上限仅为分配；实际分辨率以 header 为准）
static constexpr uint32_t kVcamMaxW     = 1280u;
static constexpr uint32_t kVcamMaxH     = 720u;
static constexpr uint32_t kVcamMaxBytes = kVcamMaxW * kVcamMaxH * 2u; // YUY2: 2B/px

#pragma pack(push, 1)
struct SharedHeaderVcam {
    uint32_t magic;            // kVcamMagic
    uint32_t version;          // kVcamVersion
    uint32_t state;            // VcamState
    uint32_t reserved0;
    uint64_t frame_index;      // 写端每帧递增；读端检测变化即新帧
    uint32_t width;            // 当前帧宽（YUY2）
    uint32_t height;           // 当前帧高
    uint32_t pitch;            // YUY2 行距 = width*2
    uint32_t format;           // 固定 1 = YUY2
    uint64_t timestamp_us;     // 写端时钟
    int32_t  consumer_count;   // filter 连接数（消费进程维护 ±1，尽力而为）
    uint8_t  pad[76];
};
static_assert(sizeof(SharedHeaderVcam) == 128, "vcam header must be 128 bytes");
#pragma pack(pop)

static constexpr uint32_t kVcamHeaderBytes = sizeof(SharedHeaderVcam);
static constexpr uint32_t kVcamFrameOffset = kVcamHeaderBytes;

inline size_t vcam_mapping_bytes() {
    return kVcamHeaderBytes + kVcamMaxBytes;
}

// 将 const 像素指针铸成可写帧区域（filter 侧复制到 IMediaSample 时使用）
inline const uint8_t* vcam_frame_data(const SharedHeaderVcam* h) {
    return reinterpret_cast<const uint8_t*>(h) + kVcamFrameOffset;
}

} // namespace lb::core