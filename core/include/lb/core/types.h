#pragma once

#include <cstdint>
#include <string>

namespace lb::core {

enum class PixelFormat : uint8_t {
    Unknown = 0,
    BGR24,
    RGB24,
    BGRA32,
    RGBA32,
    YUY2,
    NV12,
};

inline const char* to_string(PixelFormat f) {
    switch (f) {
        case PixelFormat::BGR24:  return "BGR24";
        case PixelFormat::RGB24:  return "RGB24";
        case PixelFormat::BGRA32: return "BGRA32";
        case PixelFormat::RGBA32: return "RGBA32";
        case PixelFormat::YUY2:   return "YUY2";
        case PixelFormat::NV12:   return "NV12";
        default:                  return "Unknown";
    }
}

// 一帧画面的视图。plane1 仅 NV12 使用（UV 交错平面）。
// timestamp_us 使用单调时钟，全管线统一。
struct FrameView {
    const void* plane0 = nullptr;
    uint32_t    stride0 = 0;
    const void* plane1 = nullptr;
    uint32_t    stride1 = 0;
    int         width = 0;
    int         height = 0;
    PixelFormat format = PixelFormat::Unknown;
    uint64_t    timestamp_us = 0;

    bool empty() const { return plane0 == nullptr || width <= 0 || height <= 0; }
};

struct CaptureDeviceInfo {
    std::string id;
    std::string name;
    bool is_default = false;
};

} // namespace lb::core