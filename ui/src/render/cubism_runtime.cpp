#include "cubism_runtime.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include "CubismFramework.hpp"

namespace lb::ui {

namespace {

std::mutex g_mutex;
int g_refs = 0;

// ---- Cubism 内存分配器 ----
class RuntimeAllocator final : public Live2D::Cubism::Framework::ICubismAllocator {
public:
    void* Allocate(const Live2D::Cubism::Framework::csmSizeType size) override {
        return std::malloc(size);
    }
    void Deallocate(void* memory) override { std::free(memory); }
    void* AllocateAligned(const Live2D::Cubism::Framework::csmSizeType size,
                          const Live2D::Cubism::Framework::csmUint32 alignment) override {
#ifdef _WIN32
        return _aligned_malloc(size, alignment);
#else
        void* p = nullptr;
        if (posix_memalign(&p, alignment, size) != 0) return nullptr;
        return p;
#endif
    }
    void DeallocateAligned(void* alignedMemory) override {
#ifdef _WIN32
        _aligned_free(alignedMemory);
#else
        std::free(alignedMemory);
#endif
    }
};

RuntimeAllocator g_allocator;

// 注意：Option 必须是持久对象——Framework 内部保存的是它的指针，
// 若放在函数栈上，返回后即成悬垂指针（表现为加载 shader 时调用野指针崩溃）
Live2D::Cubism::Framework::CubismFramework::Option g_option;

// ---- Framework 回调：文件加载 / 释放 / 日志 ----
Live2D::Cubism::Framework::csmByte* load_file_cb(std::string path,
                                                 Live2D::Cubism::Framework::csmSizeInt* size) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) {
        if (size) *size = 0;
        return nullptr;
    }
    fseek(f, 0, SEEK_END);
    const long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len <= 0) {
        fclose(f);
        if (size) *size = 0;
        return nullptr;
    }
    auto* buf = static_cast<Live2D::Cubism::Framework::csmByte*>(
        std::malloc(static_cast<size_t>(len)));
    const size_t got = fread(buf, 1, static_cast<size_t>(len), f);
    fclose(f);
    if (size) *size = static_cast<Live2D::Cubism::Framework::csmSizeInt>(got);
    return buf;
}

void release_bytes_cb(Live2D::Cubism::Framework::csmByte* bytes) { std::free(bytes); }

void log_cb(const Live2D::Cubism::Framework::csmChar* msg) {
    // Cubism 日志较啰嗦，只转发错误信息
    if (msg && std::strstr(msg, "Error")) std::fprintf(stderr, "[cubism] %s\n", msg);
}

} // namespace

bool CubismRuntime::acquire() {
    std::lock_guard<std::mutex> lock(g_mutex);
    using namespace Live2D::Cubism::Framework;
    if (g_refs == 0) {
        g_option.LogFunction = log_cb;
        g_option.LoggingLevel = CubismFramework::Option::LogLevel_Warning;
        g_option.LoadFileFunction = load_file_cb;
        g_option.ReleaseBytesFunction = release_bytes_cb;
        if (!CubismFramework::StartUp(&g_allocator, &g_option)) return false;
        CubismFramework::Initialize();
    }
    ++g_refs;
    return true;
}

void CubismRuntime::release() {
    std::lock_guard<std::mutex> lock(g_mutex);
    if (g_refs == 0) return;
    if (--g_refs == 0) {
        Live2D::Cubism::Framework::CubismFramework::Dispose();
        Live2D::Cubism::Framework::CubismFramework::CleanUp();
    }
}

bool CubismRuntime::ready() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_refs > 0;
}

CubismRuntime::FileBytes CubismRuntime::read_file(const std::string& path) {
    FileBytes out;
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return out;
    fseek(f, 0, SEEK_END);
    const long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len > 0) {
        out.data.resize(static_cast<size_t>(len));
        const size_t got = fread(out.data.data(), 1, static_cast<size_t>(len), f);
        out.data.resize(got);
    }
    fclose(f);
    return out;
}

} // namespace lb::ui
