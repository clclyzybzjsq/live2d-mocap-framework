#include "lb/win/win_face_tracker.h"

#include "lb/core/ipc_protocol.h"

#include <windows.h>

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace lb::win {

namespace {

std::string env_or(const char* key, const std::string& fallback) {
    const char* v = std::getenv(key);
    return (v && *v) ? std::string(v) : fallback;
}

// exe 所在目录（含结尾反斜杠）。分发版按"先找 exe 同级目录"来定位边车，
// 这样拷到任何路径都能跑，不依赖开发机上的绝对路径。
std::string exe_dir() {
    char buf[MAX_PATH * 2] = {};
    const DWORD n = GetModuleFileNameA(nullptr, buf, sizeof(buf));
    if (n == 0 || n >= sizeof(buf)) return std::string();
    std::string path(buf, n);
    const size_t slash = path.find_last_of("\\/");
    return (slash == std::string::npos) ? std::string() : path.substr(0, slash + 1);
}

std::string file_exists(const std::string& p) {
    const DWORD attr = GetFileAttributesA(p.c_str());
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY)) ? p
                                                                                  : std::string();
}

// 按优先级挑第一个存在的候选路径
std::string first_existing(std::initializer_list<std::string> candidates) {
    for (const std::string& c : candidates) {
        if (!c.empty() && !file_exists(c).empty()) return c;
    }
    return std::string();
}

// 在 PATH 上查找可执行文件（用户按常规方式安装 Python 时靠它找到解释器）
std::string find_on_path(const char* exe_name) {
    char buf[MAX_PATH * 2] = {};
    const DWORD n = SearchPathA(nullptr, exe_name, nullptr, sizeof(buf), buf, nullptr);
    return (n > 0 && n < sizeof(buf)) ? std::string(buf) : std::string();
}

} // namespace

WinFaceTracker::~WinFaceTracker() {
    shutdown();
}

bool WinFaceTracker::initialize(const lb::core::InferenceConfig& config) {
    shutdown();

    const std::string exe = exe_dir();
    // 解释器查找顺序：
    //   环境变量 > exe 同级 python\python.exe（便携环境）> PATH 上的 python.exe > 开发机默认
    const std::string python = first_existing({
        env_or("LB_SIDECAR_PYTHON", std::string()),
        exe + "python\\python.exe",
        find_on_path("python.exe"),
        "D:\\deps\\venv\\Scripts\\python.exe",
    });
    // 边车脚本：环境变量 > exe 同级 inference\tracker_server.py > 开发机默认
    const std::string script = first_existing({
        env_or("LB_SIDECAR_SCRIPT", std::string()),
        exe + "inference\\tracker_server.py",
        "D:\\ournotes_vtube\\inference\\tracker_server.py",
    });
    if (python.empty() || script.empty()) {
        // 让上层能给出可读的原因，而不是"启动了但一直没有结果"
        std::fprintf(stderr,
                     "lb_win: 找不到推理边车（python=%s script=%s）。"
                     "可用 LB_SIDECAR_PYTHON / LB_SIDECAR_SCRIPT 指定。\n",
                     python.empty() ? "(未找到)" : python.c_str(),
                     script.empty() ? "(未找到)" : script.c_str());
        return false;
    }

    map_handle_ = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE,
                                     0, lb::ipc::kSharedTotal, shm_name_.c_str());
    if (!map_handle_) return false;
    map_view_ = MapViewOfFile(map_handle_, FILE_MAP_ALL_ACCESS, 0, 0, 0);
    if (!map_view_) {
        CloseHandle(map_handle_);
        map_handle_ = nullptr;
        return false;
    }

    std::memset(map_view_, 0, lb::ipc::kSharedTotal);
    auto* hdr = static_cast<lb::ipc::SharedHeader*>(map_view_);
    hdr->magic = lb::ipc::kMagic;
    hdr->version = lb::ipc::kVersion;
    hdr->state = lb::ipc::kStateIdle;

    // 拉起边车进程
    const std::string cmdline = "\"" + python + "\" \"" + script
                              + "\" --shm " + shm_name_
                              + " --model-dir \"" + config.model_dir + "\"";
    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    if (CreateProcessA(nullptr, const_cast<char*>(cmdline.c_str()), nullptr, nullptr,
                       FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        child_ = pi.hProcess;
        CloseHandle(pi.hThread);
    } else {
        child_ = nullptr; // 边车不可用；process 将失败，UI 给出状态
    }

    initialized_ = true;
    MemoryBarrier();
    hdr->state = lb::ipc::kStateRunning;
    MemoryBarrier();
    return true;
}

bool WinFaceTracker::process(const lb::core::FrameView& frame, lb::core::RawFaceFrame& out) {
    if (!initialized_ || !map_view_) return false;

    const uint32_t bpp = frame.format == lb::core::PixelFormat::BGRA32 ? 4u
                       : frame.format == lb::core::PixelFormat::BGR24 ? 3u : 0u;
    if (bpp == 0u) return false;
    if (frame.width <= 0 || frame.height <= 0) return false;
    const uint32_t pitch = frame.stride0;
    if (pitch > lb::ipc::kMaxPitch) return false;
    const size_t bytes = static_cast<size_t>(pitch) * frame.height;
    if (bytes > lb::ipc::kFrameBytes) return false;

    auto* hdr = static_cast<lb::ipc::SharedHeader*>(map_view_);
    auto* frame_dst = static_cast<uint8_t*>(map_view_) + lb::ipc::kFrameOffset;
    const uint8_t* src = static_cast<const uint8_t*>(frame.plane0);
    const size_t row_bytes = static_cast<size_t>(frame.width) * bpp;

    MemoryBarrier();
    for (int y = 0; y < frame.height; ++y) {
        std::memcpy(frame_dst + static_cast<size_t>(y) * pitch,
                    src + static_cast<size_t>(y) * frame.stride0, row_bytes);
    }
    MemoryBarrier();

    const uint32_t new_index = hdr->frame_index + 1;
    hdr->frame_index = new_index;
    hdr->width = static_cast<uint32_t>(frame.width);
    hdr->height = static_cast<uint32_t>(frame.height);
    hdr->pitch = pitch;
    hdr->format = static_cast<uint32_t>(frame.format);
    hdr->timestamp_us = frame.timestamp_us;
    MemoryBarrier();

    // 等待边车结果（上限 500ms，覆盖边车满载/冷启动）
    const uint32_t wanted = new_index;
    bool ok = false;
    for (int i = 0; i < 500; ++i) {
        MemoryBarrier();
        if (hdr->result_index >= wanted) { ok = true; break; }
        Sleep(1);
    }
    if (!ok) return false;

    const auto* r = reinterpret_cast<const lb::ipc::SharedResult*>(
        static_cast<const uint8_t*>(map_view_) + lb::ipc::kResultOffset);

    out.face_landmarks.assign(r->face_landmarks,
                              r->face_landmarks + r->face_landmark_count * 3u);
    out.face_blendshapes.assign(r->face_blendshapes,
                                r->face_blendshapes + r->face_blendshape_count);
    out.pose_landmarks.assign(r->pose_landmarks,
                              r->pose_landmarks + r->pose_landmark_count * 3u);
    out.face_detected = r->face_detected != 0;
    out.upper_body_detected = r->upper_body_detected != 0;
    out.face_confidence = r->confidence;
    out.timestamp_us = r->timestamp_us;
    return true;
}

void WinFaceTracker::shutdown() {
    if (map_view_ && initialized_) {
        auto* hdr = static_cast<lb::ipc::SharedHeader*>(map_view_);
        hdr->state = lb::ipc::kStateQuitting;
        MemoryBarrier();
        if (child_) {
            WaitForSingleObject(child_, 2000);
        }
    }
    if (child_) {
        CloseHandle(child_);
        child_ = nullptr;
    }
    if (map_view_) {
        UnmapViewOfFile(map_view_);
        map_view_ = nullptr;
    }
    if (map_handle_) {
        CloseHandle(map_handle_);
        map_handle_ = nullptr;
    }
    initialized_ = false;
}

} // namespace lb::win