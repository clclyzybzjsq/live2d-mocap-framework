#include "lb/win/win_capture_source.h"

#include <windows.h>

#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace lb::win {

namespace {

// 引用置空，避免失败路径残留悬垂指针导致二次释放（M1 真实采集崩溃根因）
template <typename T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }

// 采集链格式：0 = 直接 BGRA（MFVideoFormat_RGB32），1 = YUY2，2 = NV12。
// MF 转换器在本机缺失（MF_E_TOPO_CODEC_NOT_FOUND），因此原色格式由库内自转。
inline int clamp_byte(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

void yuy2_to_bgra(const uint8_t* src, int w, int h, int src_stride, uint8_t* dst) {
    for (int y = 0; y < h; ++y) {
        const uint8_t* row = src + static_cast<size_t>(y) * src_stride;
        uint8_t* d = dst + static_cast<size_t>(y) * w * 4;
        for (int x = 0; x < w; x += 2) {
            const uint8_t* p = row + static_cast<size_t>(x) * 2;
            const int y0 = p[0] - 16, u = p[1] - 128, y1 = p[2] - 16, v = p[3] - 128;
            d[x * 4 + 0] = static_cast<uint8_t>(clamp_byte(((298 * y0 + 516 * u) >> 8)));
            d[x * 4 + 1] = static_cast<uint8_t>(clamp_byte(((298 * y0 - 100 * u - 208 * v) >> 8)));
            d[x * 4 + 2] = static_cast<uint8_t>(clamp_byte(((298 * y0 + 409 * v) >> 8)));
            d[x * 4 + 3] = 255;
            d[x * 4 + 4] = static_cast<uint8_t>(clamp_byte(((298 * y1 + 516 * u) >> 8)));
            d[x * 4 + 5] = static_cast<uint8_t>(clamp_byte(((298 * y1 - 100 * u - 208 * v) >> 8)));
            d[x * 4 + 6] = static_cast<uint8_t>(clamp_byte(((298 * y1 + 409 * v) >> 8)));
            d[x * 4 + 7] = 255;
        }
    }
}

void nv12_to_bgra(const uint8_t* y_plane, const uint8_t* uv_plane,
                  int w, int h, int y_stride, int uv_stride, uint8_t* dst) {
    for (int y = 0; y < h; ++y) {
        const uint8_t* y_row = y_plane + static_cast<size_t>(y) * y_stride;
        const uint8_t* uv_row = uv_plane + static_cast<size_t>(y / 2) * uv_stride;
        uint8_t* d = dst + static_cast<size_t>(y) * w * 4;
        for (int x = 0; x < w; ++x) {
            const int yy = y_row[x] - 16;
            const int u = uv_row[(x / 2) * 2 + 0] - 128;
            const int v = uv_row[(x / 2) * 2 + 1] - 128;
            d[x * 4 + 0] = static_cast<uint8_t>(clamp_byte(((298 * yy + 516 * u) >> 8)));
            d[x * 4 + 1] = static_cast<uint8_t>(clamp_byte(((298 * yy - 100 * u - 208 * v) >> 8)));
            d[x * 4 + 2] = static_cast<uint8_t>(clamp_byte(((298 * yy + 409 * v) >> 8)));
            d[x * 4 + 3] = 255;
        }
    }
}

std::once_flag g_mf_startup;
void ensure_mf_startup() {
    std::call_once(g_mf_startup, []() { MFStartup(MF_VERSION); });
}

std::wstring utf8_to_wide(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                                      nullptr, 0);
    std::wstring out(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()),
                        out.data(), n);
    return out;
}

std::string wide_to_utf8(const wchar_t* s) {
    if (!s || !*s) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? n - 1 : 0, '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), n, nullptr, nullptr);
    return out;
}

void ensure_com() {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
}

} // namespace

struct WinCaptureSource::Impl {
    IMFSourceReader* reader = nullptr;
    int frame_w = 0;
    int frame_h = 0;
    int format = 0; // 0=RGB32, 1=YUY2, 2=NV12
    std::vector<uint8_t> frame_buf;
    std::mutex frame_mutex;
};

WinCaptureSource::WinCaptureSource() : impl_(std::make_unique<Impl>()) {}

WinCaptureSource::~WinCaptureSource() {
    close();
}

std::vector<lb::core::CaptureDeviceInfo> WinCaptureSource::enumerate_devices() {
    std::vector<lb::core::CaptureDeviceInfo> out;
    ensure_com();
    ensure_mf_startup();

    IMFAttributes* attrs = nullptr;
    if (FAILED(MFCreateAttributes(&attrs, 1))) return out;
    attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                   MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);

    IMFActivate** devices = nullptr;
    UINT32 count = 0;
    if (SUCCEEDED(MFEnumDeviceSources(attrs, &devices, &count))) {
        for (UINT32 i = 0; i < count; ++i) {
            lb::core::CaptureDeviceInfo info;
            WCHAR* name = nullptr;
            WCHAR* link = nullptr;
            UINT32 name_len = 0;
            UINT32 link_len = 0;
            if (SUCCEEDED(devices[i]->GetAllocatedString(
                    MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, &name_len))) {
                info.name = wide_to_utf8(name);
                CoTaskMemFree(name);
            }
            if (SUCCEEDED(devices[i]->GetAllocatedString(
                    MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                    &link, &link_len))) {
                info.id = wide_to_utf8(link);
                CoTaskMemFree(link);
            }
            info.is_default = (i == 0);
            if (!info.id.empty()) out.push_back(std::move(info));
            devices[i]->Release();
        }
    }
    CoTaskMemFree(devices);
    SafeRelease(attrs);
    return out;
}

bool WinCaptureSource::open(const std::string& device_id,
                            const lb::core::CaptureConfig& config) {
    close();
    config_ = config;
    mirror_.store(config.mirror);
    if (device_id == synthetic_device_id()) {
        synthetic_ = true;
        return true;
    }
    synthetic_ = false;

    ensure_com();
    ensure_mf_startup();

    IMFAttributes* attrs = nullptr;
    IMFMediaSource* source = nullptr;
    HRESULT hr = MFCreateAttributes(&attrs, 2);
    if (SUCCEEDED(hr)) {
        attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                       MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        attrs->SetString(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_SYMBOLIC_LINK,
                         utf8_to_wide(device_id).c_str());
        hr = MFCreateDeviceSource(attrs, &source);
    }
    SafeRelease(attrs);
    if (FAILED(hr)) return false;

    hr = MFCreateSourceReaderFromMediaSource(source, nullptr, &impl_->reader);
    SafeRelease(source);
    if (FAILED(hr)) return false;

    // 按优先级尝试输出格式与尺寸（本机缺 RGB32 转换 MFT，故原生格式兜底自转）
    struct TypeTry {
        const GUID* subtype;
        const char* name;
        int fmt_index;
    };
    constexpr TypeTry tries[] = {
        { &MFVideoFormat_RGB32, "RGB32", 0 },
        { &MFVideoFormat_YUY2,  "YUY2",  1 },
        { &MFVideoFormat_NV12,  "NV12",  2 },
    };

    int cur_w = 0;
    int cur_h = 0;
    int cur_format = -1;
    // 先试用户尺寸，再回退设备原生尺寸
    for (int pass = 0; pass < 2 && cur_format < 0; ++pass) {
        for (const auto& t : tries) {
            IMFMediaType* mt = nullptr;
            if (FAILED(MFCreateMediaType(&mt))) continue;
            mt->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
            mt->SetGUID(MF_MT_SUBTYPE, *t.subtype);
            if (pass == 0) {
                MFSetAttributeSize(mt, MF_MT_FRAME_SIZE, config.width, config.height);
                MFSetAttributeRatio(mt, MF_MT_FRAME_RATE, config.fps, 1);
            }
            hr = impl_->reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                                    nullptr, mt);
            SafeRelease(mt);
            if (SUCCEEDED(hr)) {
                UINT32 w = 0, h = 0;
                IMFMediaType* got = nullptr;
                if (SUCCEEDED(impl_->reader->GetCurrentMediaType(
                        MF_SOURCE_READER_FIRST_VIDEO_STREAM, &got))) {
                    MFGetAttributeSize(got, MF_MT_FRAME_SIZE, &w, &h);
                }
                SafeRelease(got);
                cur_w = w > 0 ? static_cast<int>(w) : config.width;
                cur_h = h > 0 ? static_cast<int>(h) : config.height;
                cur_format = t.fmt_index;
                std::fprintf(stderr, "lb_win: capture format=%s %dx%d\n",
                             t.name, cur_w, cur_h);
                break;
            }
        }
    }
    if (cur_format < 0) {
        SafeRelease(impl_->reader);
        return false;
    }

    impl_->frame_w = cur_w;
    impl_->frame_h = cur_h;
    impl_->format = cur_format;
    const size_t bytes = static_cast<size_t>(cur_w) * cur_h * 4;
    std::lock_guard<std::mutex> lock(impl_->frame_mutex);
    impl_->frame_buf.resize(bytes);
    return true;
}

void WinCaptureSource::close() {
    stop();
    if (impl_->reader) {
        SafeRelease(impl_->reader);
    }
    synthetic_ = false;
    config_ = {};
}

bool WinCaptureSource::start(lb::core::FrameCallback on_frame) {
    if (running_ || worker_.joinable()) return false;
    if (!synthetic_ && !impl_->reader) return false;
    callback_ = std::move(on_frame);
    running_ = true;
    worker_ = std::thread([this]() { worker_loop(); });
    return true;
}

void WinCaptureSource::stop() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
    callback_ = nullptr;
}

bool WinCaptureSource::is_running() const {
    return running_;
}

lb::core::CaptureConfig WinCaptureSource::config() const {
    return config_;
}

void WinCaptureSource::set_mirror(bool enabled) {
    config_.mirror = enabled;
    mirror_.store(enabled);
}

namespace {
// 水平镜像（原地翻转）。BGRA32，每像素 4 字节，逐行前后对调。
// 放在采集端而不是渲染端：这样推理拿到的是镜像后的画面，
// 转头方向也会跟着镜像，才是"照镜子"的观感。
void mirror_bgra_inplace(uint8_t* buf, int w, int h) {
    for (int y = 0; y < h; ++y) {
        uint8_t* row = buf + static_cast<size_t>(y) * static_cast<size_t>(w) * 4u;
        for (int x = 0; x < w / 2; ++x) {
            uint8_t* a = row + static_cast<size_t>(x) * 4u;
            uint8_t* b = row + static_cast<size_t>(w - 1 - x) * 4u;
            for (int c = 0; c < 4; ++c) std::swap(a[c], b[c]);
        }
    }
}
} // namespace

void WinCaptureSource::worker_loop() {
    if (synthetic_) {
        ensure_com();
        const int w = config_.width;
        const int h = config_.height;
        const int fps = std::max(config_.fps, 1);
        const auto period = std::chrono::milliseconds(1000 / fps);

        std::lock_guard<std::mutex> lock(impl_->frame_mutex);
        impl_->frame_buf.assign(static_cast<size_t>(w) * h * 4, 0);
        auto& buf = impl_->frame_buf;

        uint64_t frame_no = 0;
        while (running_) {
            const auto begin = std::chrono::steady_clock::now();
            // 简单测试画面：深蓝底 + 运动的"脸"（肤色椭圆 + 眨眼 + 嘴）
            const double t = frame_no * 0.1;
            const int face_cx = w / 2;
            const int face_cy = h / 2 + static_cast<int>(16.0 * std::sin(t));
            const int face_r = std::min(w, h) / 3;
            const int blink_open = (static_cast<int>(frame_no / 30) % 2 == 0) ? 16 : 2;

            for (int y = 0; y < h; ++y) {
                uint8_t* row = buf.data() + static_cast<size_t>(y) * w * 4;
                for (int x = 0; x < w; ++x) {
                    const bool in_face = (x - face_cx) * (x - face_cx)
                                       + (y - face_cy) * (y - face_cy)
                                       <= face_r * face_r;
                    if (in_face) {
                        row[x * 4 + 0] = 200; // B
                        row[x * 4 + 1] = 176; // G
                        row[x * 4 + 2] = 150; // R
                        row[x * 4 + 3] = 255; // A
                    } else {
                        row[x * 4 + 0] = 18;  // 深蓝底
                        row[x * 4 + 1] = 22;
                        row[x * 4 + 2] = 34;
                        row[x * 4 + 3] = 255;
                    }
                }
            }
            // 眼睛
            const int eye_dy = -face_r / 3;
            for (int side = -1; side <= 1; side += 2) {
                const int ex = face_cx + side * face_r / 3;
                const int ey = face_cy + eye_dy;
                for (int yy = ey - blink_open / 2; yy <= ey + blink_open / 2; ++yy) {
                    for (int xx = ex - face_r / 8; xx <= ex + face_r / 8; ++xx) {
                        if (xx < 0 || xx >= w || yy < 0 || yy >= h) continue;
                        uint8_t* p = buf.data() + (static_cast<size_t>(yy) * w + xx) * 4;
                        p[0] = p[1] = p[2] = 20;
                    }
                }
            }
            // 嘴（张合）
            const int mouth_open = static_cast<int>(4 + 6 * std::sin(frame_no * 0.5));
            for (int yy = face_cy + face_r / 3 - mouth_open / 2;
                 yy <= face_cy + face_r / 3 + mouth_open / 2; ++yy) {
                for (int xx = face_cx - face_r / 5; xx <= face_cx + face_r / 5; ++xx) {
                    if (xx < 0 || xx >= w || yy < 0 || yy >= h) continue;
                    uint8_t* p = buf.data() + (static_cast<size_t>(yy) * w + xx) * 4;
                    p[0] = p[1] = p[2] = 60;
                }
            }

            if (mirror_.load()) mirror_bgra_inplace(buf.data(), w, h);

            lb::core::FrameView view;
            view.plane0 = buf.data();
            view.stride0 = static_cast<uint32_t>(w * 4);
            view.width = w;
            view.height = h;
            view.format = lb::core::PixelFormat::BGRA32;
            view.timestamp_us = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
            if (callback_) callback_(view);

            ++frame_no;
            const auto elapsed = std::chrono::steady_clock::now() - begin;
            if (elapsed < period) {
                std::this_thread::sleep_for(period - elapsed);
            }
        }
        return;
    }

    // ---- 真实摄像头：MF SourceReader 同步读帧 ----
    ensure_com();
    while (running_ && impl_->reader) {
        IMFSample* sample = nullptr;
        DWORD stream_flags = 0;
        HRESULT hr = impl_->reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                               0, nullptr, &stream_flags, nullptr,
                                               &sample);
        if (FAILED(hr) || stream_flags & MF_SOURCE_READERF_ENDOFSTREAM) {
            if (sample) sample->Release();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            continue;
        }
        if (!sample) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }

        IMFMediaBuffer* buffer = nullptr;
        if (SUCCEEDED(sample->ConvertToContiguousBuffer(&buffer))) {
            BYTE* data = nullptr;
            DWORD len = 0;
            if (SUCCEEDED(buffer->Lock(&data, nullptr, &len))) {
                const int w = impl_->frame_w;
                const int h = impl_->frame_h;
                if (w > 0 && h > 0 && data) {
                    std::lock_guard<std::mutex> lock(impl_->frame_mutex);
                    uint8_t* dst = impl_->frame_buf.data();
                    const size_t dst_bytes = static_cast<size_t>(w) * h * 4;
                    bool filled = false;
                    if (impl_->format == 0) { // RGB32 直接拷贝
                        const size_t need = dst_bytes;
                        if (len >= need) {
                            for (int y = 0; y < h; ++y) {
                                std::memcpy(dst + static_cast<size_t>(y) * w * 4,
                                            data + static_cast<size_t>(y) * w * 4,
                                            static_cast<size_t>(w) * 4);
                            }
                            filled = true;
                        }
                    } else if (impl_->format == 1) { // YUY2 自转 BGRA
                        if (len >= static_cast<size_t>(w) * h * 2) {
                            yuy2_to_bgra(data, w, h, w * 2, dst);
                            filled = true;
                        }
                    } else { // NV12 自转 BGRA
                        if (len >= static_cast<size_t>(w) * h * 3 / 2) {
                            nv12_to_bgra(data, data + static_cast<size_t>(w) * h,
                                         w, h, w, w, dst);
                            filled = true;
                        }
                    }
                    if (filled) {
                        if (mirror_.load()) mirror_bgra_inplace(dst, w, h);
                        lb::core::FrameView view;
                        view.plane0 = dst;
                        view.stride0 = static_cast<uint32_t>(w * 4);
                        view.width = w;
                        view.height = h;
                        view.format = lb::core::PixelFormat::BGRA32;
                        view.timestamp_us =
                            std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now().time_since_epoch()).count();
                        if (callback_) callback_(view);
                    }
                }
                buffer->Unlock();
            }
            SafeRelease(buffer);
        }
        SafeRelease(sample);
    }
}

} // namespace lb::win