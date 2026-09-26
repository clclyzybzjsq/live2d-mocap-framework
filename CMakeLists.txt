cmake_minimum_required(VERSION 3.21)
project(ournotes_vtube LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LB_BUILD_UI "Build the Qt6 desktop UI" ON)

# 核心层：平台无关的接口与纯逻辑
add_subdirectory(core)

# 平台适配层：Windows 实现（采集 / 虚拟摄像头 / 浮窗 / GPU / 渲染 / 推理客户端）
add_subdirectory(platform/windows)

# 应用层：Qt6 桌面界面（黑色主题，无表情符号，按 docs/assessment-plan.md 第七节）
if(LB_BUILD_UI)
    find_package(Qt6 QUIET COMPONENTS Widgets)
    if(Qt6_FOUND)
        add_subdirectory(ui)
    else()
        message(STATUS "Qt6 not found - UI target skipped (use scripts/build.ps1)")
    endif()
endif()

# M1 管线冒烟测试（可选，验证 采集 -> 推理 -> 求解 -> 映射 全链路）
option(LB_BUILD_SMOKE "Build pipeline smoke test" OFF)
if(LB_BUILD_SMOKE)
    add_executable(lb_smoke tools/pipe_smoke.cpp)
    target_link_libraries(lb_smoke PRIVATE lb_core lb_win)
    target_compile_features(lb_smoke PRIVATE cxx_std_17)
endif()

# M2 虚拟摄像头注册可见性探测 + 图级功能测试（可选）
option(LB_BUILD_VCAM_PROBE "Build vcam registration probe" OFF)
option(LB_BUILD_VCAM_FLOW "Build vcam graph flow test" OFF)
if(LB_BUILD_VCAM_PROBE)
    add_executable(lb_vcam_probe tools/vcam_probe.cpp)
    target_link_libraries(lb_vcam_probe PRIVATE strmiids ole32 oleaut32 uuid)
    target_compile_features(lb_vcam_probe PRIVATE cxx_std_17)
endif()
if(LB_BUILD_VCAM_FLOW)
    add_executable(lb_vcam_flow tools/vcam_flow_test.cpp)
    target_link_libraries(lb_vcam_flow PRIVATE strmiids ole32 oleaut32 uuid)
    target_compile_features(lb_vcam_flow PRIVATE cxx_std_17)
    if(NOT MSVC)
        # MinGW 默认导出全部符号；进程内系统 COM（quartz）会绑定到错误导出导致崩溃
        target_link_options(lb_vcam_flow PRIVATE "-Wl,--exclude-all-symbols")
    endif()

    add_executable(lb_vcam_write tools/vcam_write_test.cpp)
    target_link_libraries(lb_vcam_write PRIVATE lb_core lb_win)
    target_compile_features(lb_vcam_write PRIVATE cxx_std_17)

    # 持续喂帧（模拟应用写端）——验证 应用 -> 共享内存 -> 消费方 的端到端链路
    add_executable(lb_vcam_feed tools/vcam_feed.cpp)
    target_link_libraries(lb_vcam_feed PRIVATE lb_core lb_win)
    target_compile_features(lb_vcam_feed PRIVATE cxx_std_17)
endif()