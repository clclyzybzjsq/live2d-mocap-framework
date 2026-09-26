#pragma once

#include <string>
#include <vector>

#include "lb/core/cubism_param_map.h"
#include "lb/core/types.h"

namespace lb::ui {

// 诊断日志：写到 exe 同目录的 cubism_debug.log。
// UI 是 GUI 程序，stderr 重定向在本环境下时有时无，写文件才可靠。
// 环境变量 LB_CUBISM_LOG=0 可关闭。
void cubism_log(const char* fmt, ...);

// Cubism Framework 的进程级初始化（StartUp / Initialize / Dispose）。
//
// Framework 内部是全局单例，多处独立调用 StartUp 会互相破坏，因此统一由这里
// 以引用计数方式管理；测试工具与应用共用同一入口。
class CubismRuntime {
public:
    // 首次调用完成 StartUp + Initialize，之后仅增加引用计数
    static bool acquire();
    // 减少引用计数，归零时 Dispose + CleanUp
    static void release();
    // 是否已初始化
    static bool ready();

    // Framework 的文件加载回调格式（供渲染器加载 model3.json / moc3 用）
    struct FileBytes {
        std::vector<unsigned char> data;
        bool ok() const { return !data.empty(); }
    };
    // 以本地 8bit 路径读取整个文件（Windows 上 fopen 使用本地代码页）
    static FileBytes read_file(const std::string& path);
};

} // namespace lb::ui
