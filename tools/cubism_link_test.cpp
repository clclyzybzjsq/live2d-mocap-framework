// cubism_link_test —— 验证 MinGW 下 Cubism Framework + Core 的编译、链接与运行。
// 这是 SDK 接入的第一道关卡：Core 官方只提供 MSVC 格式 .lib，本项目用
// scripts/build_cubism_core_lib.ps1 生成的 MinGW 导入库；Framework 由官方
// CMakeLists 以 add_subdirectory 方式引入（OpenGL 后端）。
#include <cstdio>
#include <cstdlib>

#include "CubismFramework.hpp"
#include "ICubismAllocator.hpp"
#include "Live2DCubismCore.hpp"

using namespace Live2D::Cubism::Framework;

namespace {

class TestAllocator final : public ICubismAllocator {
public:
    void* Allocate(const csmSizeType size) override { return std::malloc(size); }
    void Deallocate(void* memory) override { std::free(memory); }
    void* AllocateAligned(const csmSizeType size, const csmUint32 alignment) override {
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

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IONBF, 0);
    printf("cubism link test\n");

    // Core（Live2DCubismCore.dll，通过生成的导入库链接）
    // 注意：Live2DCubismCore.hpp 把 Core 包进了 Live2D::Cubism::Core 命名空间
    const unsigned int core_version = Live2D::Cubism::Core::csmGetVersion();
    printf("  Core version: %u\n", core_version);
    if (core_version == 0) {
        printf("FAIL: Core dll 未正确加载（csmGetVersion 返回 0）\n");
        return 1;
    }

    // Framework
    TestAllocator allocator;
    if (!CubismFramework::StartUp(&allocator, nullptr)) {
        printf("FAIL: CubismFramework::StartUp\n");
        return 1;
    }
    CubismFramework::Initialize();
    printf("  Framework StartUp + Initialize OK\n");
    CubismFramework::Dispose();
    CubismFramework::CleanUp();
    printf("  Framework Dispose + CleanUp OK\n");

    printf("RESULT OK\n");
    return 0;
}
