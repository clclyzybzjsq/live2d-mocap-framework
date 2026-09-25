#include "lb/win/win_virtual_camera.h"

#include <windows.h>
#include <winreg.h>

#include <cstdio>
#include <cstring>
#include <mutex>

#include "lb/core/vcam_protocol.h"

namespace lb::win {

namespace {

constexpr const wchar_t* kVcamShmName  = L"lb_vcam_frames";
constexpr const char*    kFilterDll    = "lb_vcam_filter.dll";
constexpr const char*    kRegClsidHklm = "Software\\Classes\\CLSID\\{2A17E0B2-2A8C-4E4A-9E5C-8B4A6D2F1C55}\\InprocServer32";
constexpr const char*    kRegClsidHkcu = "Software\\Classes\\CLSID\\{2A17E0B2-2A8C-4E4A-9E5C-8B4A6D2F1C55}\\InprocServer32";

// DirectShow 设备枚举（ICreateDevEnum）只读 HKLM 视图；HKLM 注册需一次管理员权限。
bool clsid_registered() {
    HKEY key = nullptr;
    LONG r = RegOpenKeyExA(HKEY_LOCAL_MACHINE, kRegClsidHklm, 0,
                           KEY_READ, &key);
    if (r != ERROR_SUCCESS) {
        // 容错：也接受 HKCU（某些工具读取）
        r = RegOpenKeyExA(HKEY_CURRENT_USER, kRegClsidHkcu, 0, KEY_READ, &key);
    }
    if (r != ERROR_SUCCESS) return false;
    RegCloseKey(key);
    return true;
}

// 调用 filter DLL 的 DllRegisterServer / DllUnregisterServer
// （应用的 exe 目录下应有 lb_vcam_filter.dll，与 exe 一起分发）
bool call_filter_dll(const char* proc_name) {
    wchar_t exe_dir[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, exe_dir, MAX_PATH);
    // 截到目录
    wchar_t* slash = wcsrchr(exe_dir, L'\\');
    if (!slash) return false;
    *(slash + 1) = L'\0';
    wchar_t dll_path[MAX_PATH] = {0};
    swprintf(dll_path, MAX_PATH, L"%ls%hs", exe_dir, kFilterDll);

    HMODULE mod = LoadLibraryW(dll_path);
    if (!mod) {
        std::fprintf(stderr, "lb_win: vcam filter dll load failed: %ls (err=%lu)\n",
                     dll_path, GetLastError());
        return false;
    }
    using Fn = HRESULT(WINAPI*)();
    Fn fn = reinterpret_cast<Fn>(GetProcAddress(mod, proc_name));
    if (!fn) {
        FreeLibrary(mod);
        return false;
    }
    HRESULT hr = fn();
    FreeLibrary(mod);
    if (FAILED(hr)) {
        std::fprintf(stderr, "lb_win: %s failed hr=0x%08lX\n", proc_name,
                     static_cast<unsigned long>(hr));
        return false;
    }
    return true;
}

} // namespace

bool WinVirtualCamera::is_registered() const {
    return clsid_registered();
}

bool WinVirtualCamera::ensure_registered() {
    if (clsid_registered()) return true;
    std::fprintf(stderr,
                 "lb_win: virtual camera not registered; need one elevated "
                 "registration (run scripts/register_vcam.ps1 as admin)\n");
    // 尽力尝试：当前进程若无管理员权限会失败，返回 false 由 UI 提示
    if (!call_filter_dll("DllRegisterServer")) {
        std::fprintf(stderr, "lb_win: DllRegisterServer failed (likely not elevated)\n");
        return false;
    }
    return clsid_registered();
}

bool WinVirtualCamera::start(const lb::core::VirtualCameraFormat& format) {
    stop();
    format_ = format;

    const size_t bytes = lb::core::vcam_mapping_bytes();
    HANDLE map = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                    0, static_cast<DWORD>(bytes), kVcamShmName);
    if (!map) {
        std::fprintf(stderr, "lb_win: vcam CreateFileMapping failed err=%lu\n", GetLastError());
        return false;
    }
    // 已存在则复用（上一次异常退出残留），重新建内容
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        std::fprintf(stderr, "lb_win: vcam shm existed, reusing/resetting\n");
    }
    void* view = MapViewOfFile(map, FILE_MAP_ALL_ACCESS, 0, 0, bytes);
    if (!view) {
        CloseHandle(map);
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    map_ = map;
    view_ = view;
    auto* hdr = static_cast<lb::core::SharedHeaderVcam*>(view_);
    std::memset(hdr, 0, sizeof(*hdr));
    hdr->magic = lb::core::kVcamMagic;
    hdr->version = lb::core::kVcamVersion;
    hdr->state = lb::core::kVcamIdle;
    hdr->frame_index = 0;
    hdr->width = static_cast<uint32_t>(format_.width);
    hdr->height = static_cast<uint32_t>(format_.height);
    hdr->pitch = static_cast<uint32_t>(format_.width) * 2u;
    hdr->format = 1; // YUY2
    hdr->consumer_count = 0;
    running_ = true;
    return true;
}

// BGRA32 -> YUY2（BT.601 近似，与采集端转换同源思路）
namespace {
inline int yuv_clamp(int v) { return v < 0 ? 0 : (v > 255 ? 255 : v); }

void bgra_to_yuy2(const uint8_t* src, size_t src_pitch, uint8_t* dst,
                  size_t dst_pitch, int w, int h) {
    for (int y = 0; y < h; ++y) {
        const uint8_t* s = src + static_cast<size_t>(y) * src_pitch;
        uint8_t* d = dst + static_cast<size_t>(y) * dst_pitch;
        for (int x = 0; x < w; x += 2) {
            const uint8_t* p0 = s + static_cast<size_t>(x) * 4;
            const uint8_t* p1 = p0 + 4;
            const int b0 = p0[0], g0 = p0[1], r0 = p0[2];
            const int b1 = p1[0], g1 = p1[1], r1 = p1[2];
            const int y0 = ((66 * r0 + 129 * g0 + 25 * b0 + 128) >> 8) + 16;
            const int y1 = ((66 * r1 + 129 * g1 + 25 * b1 + 128) >> 8) + 16;
            const int u  = ((-38 * r0 - 74 * g0 + 112 * b0 + 128) >> 8) + 128;
            const int v  = ((112 * r0 - 94 * g0 - 18 * b0 + 128) >> 8) + 128;
            d[x * 2 + 0] = static_cast<uint8_t>(yuv_clamp(y0));
            d[x * 2 + 1] = static_cast<uint8_t>(yuv_clamp(u));
            d[x * 2 + 2] = static_cast<uint8_t>(yuv_clamp(y1));
            d[x * 2 + 3] = static_cast<uint8_t>(yuv_clamp(v));
        }
    }
}
} // namespace

bool WinVirtualCamera::push_frame(const lb::core::FrameView& frame) {
    if (!running_ || !view_) return false;
    if (frame.format != lb::core::PixelFormat::BGRA32) return false;

    std::lock_guard<std::mutex> lock(mutex_);
    if (frame.width > static_cast<int>(lb::core::kVcamMaxW) ||
        frame.height > static_cast<int>(lb::core::kVcamMaxH)) {
        return false;
    }
    auto* hdr = static_cast<lb::core::SharedHeaderVcam*>(view_);
    if (hdr->magic != lb::core::kVcamMagic) return false;

    uint8_t* dst = const_cast<uint8_t*>(lb::core::vcam_frame_data(hdr));
    const size_t dst_pitch = static_cast<size_t>(frame.width) * 2u;
    const size_t need = dst_pitch * static_cast<size_t>(frame.height);
    if (need > lb::core::kVcamMaxBytes) return false;
    bgra_to_yuy2(static_cast<const uint8_t*>(frame.plane0), frame.stride0,
                 dst, dst_pitch, frame.width, frame.height);

    hdr->width = static_cast<uint32_t>(frame.width);
    hdr->height = static_cast<uint32_t>(frame.height);
    hdr->pitch = static_cast<uint32_t>(dst_pitch);
    hdr->timestamp_us = frame.timestamp_us;
    hdr->state = lb::core::kVcamRunning;
    // 发布：先写像素与尺寸，再递增 frame_index（MemoryBarrier 保证顺序）
    MemoryBarrier();
    hdr->frame_index += 1;
    return true;
}

void WinVirtualCamera::stop() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (view_) {
        UnmapViewOfFile(view_);
        view_ = nullptr;
    }
    if (map_) {
        CloseHandle(map_);
        map_ = nullptr;
    }
    running_ = false;
}

bool WinVirtualCamera::is_running() const {
    return running_;
}

bool WinVirtualCamera::has_consumer() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!view_) return false;
    auto* hdr = static_cast<lb::core::SharedHeaderVcam*>(view_);
    if (hdr->magic != lb::core::kVcamMagic) return false;
    return hdr->consumer_count > 0;
}

void WinVirtualCamera::unregister_device() {
    call_filter_dll("DllUnregisterServer");
}

} // namespace lb::win