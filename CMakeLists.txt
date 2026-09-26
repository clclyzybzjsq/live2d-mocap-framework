cmake_minimum_required(VERSION 3.21)
project(ournotes_vtube LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(LB_BUILD_UI "Build the Qt6 desktop UI" ON)

# ---- Live2D Cubism SDK 接入（M3，可选）----
# 需要先运行 scripts/build_cubism_core_lib.ps1 生成 MinGW 可用的 Core 导入库
# （官方只提供 MSVC 格式 .lib）。
option(LB_WITH_CUBISM "Enable Live2D Cubism rendering" OFF)
set(LB_CUBISM_SDK_DIR "D:/deps/cubism" CACHE PATH "Live2D Cubism SDK root")
set(LB_CUBISM_CORE_DLL "${LB_CUBISM_SDK_DIR}/Core/dll/windows/x86_64/Live2DCubismCore.dll")
set(LB_CUBISM_CORE_IMPLIB "${CMAKE_BINARY_DIR}/cubism_link/libLive2DCubismCore.a")
# Cubism 的 OpenGL 后端 include <GL/glew.h>（MinGW 不自带 GLEW）
set(LB_GLEW_DIR "D:/deps/glew/glew" CACHE PATH "GLEW root (src/glew.c, include/GL)")
set(LB_DIRECTXMATH_INC "D:/deps/directxmath/Inc" CACHE PATH "Official DirectXMath include dir (D3D11 backend)")

if(LB_WITH_CUBISM)
    if(NOT EXISTS "${LB_CUBISM_SDK_DIR}/Core/include/Live2DCubismCore.h")
        message(FATAL_ERROR "Cubism SDK 不完整：缺少 ${LB_CUBISM_SDK_DIR}/Core/include/Live2DCubismCore.h")
    endif()
    if(NOT EXISTS "${LB_CUBISM_SDK_DIR}/Framework/CMakeLists.txt")
        message(FATAL_ERROR "Cubism SDK 不完整：缺少 ${LB_CUBISM_SDK_DIR}/Framework/CMakeLists.txt")
    endif()
    if(NOT EXISTS "${LB_CUBISM_CORE_IMPLIB}")
        message(FATAL_ERROR "缺少 Core 导入库 ${LB_CUBISM_CORE_IMPLIB}；先运行 scripts/build_cubism_core_lib.ps1")
    endif()
    if(NOT EXISTS "${LB_GLEW_DIR}/src/glew.c")
        message(FATAL_ERROR "缺少 GLEW 源码 ${LB_GLEW_DIR}/src/glew.c（见 docs/assessment-plan.md M3 节）")
    endif()

    # GLEW 静态库（C 源码，项目内编译，避免外部预编译库的 ABI 问题）
    add_library(lb_glew STATIC "${LB_GLEW_DIR}/src/glew.c")
    target_include_directories(lb_glew PUBLIC "${LB_GLEW_DIR}/include")
    target_compile_definitions(lb_glew PUBLIC GLEW_STATIC)
    target_compile_definitions(lb_glew PRIVATE GLEW_NO_GLU)

    # 官方 Framework 的 Rendering/CMakeLists.txt 用 FRAMEWORK_SOURCE 选择渲染后端目录
    set(FRAMEWORK_SOURCE "${LB_CUBISM_SDK_DIR}/Framework/src/Rendering/OpenGL")
    set(RENDER_INCLUDE_PATH "")
    add_subdirectory("${LB_CUBISM_SDK_DIR}/Framework"
                     "${CMAKE_BINARY_DIR}/cubism_framework" EXCLUDE_FROM_ALL)
    # 桌面 OpenGL 目标（Cubism 后端据此选择 GL 头）
    # OpenGL 后端（配合 Qt 的 QOpenGLContext：实测该组合可正常渲染）
    target_compile_definitions(Framework PRIVATE CSM_TARGET_WIN_GL)
    # MinGW 自带的 DirectXMath 是简化版（缺 XMMATRIX），必须用官方完整版，
    # 且要排在系统 include 之前
    if(EXISTS "${LB_DIRECTXMATH_INC}/DirectXMath.h")
        target_include_directories(Framework BEFORE PUBLIC "${LB_DIRECTXMATH_INC}")
        # Cubism 的 D3D11 代码按 MSVC 的 __m128 union 语义访问 XMVECTOR
        # （x.r[0].m128_f32[0]），GCC/MinGW 的 __m128 是内置类型没有该成员，
        # 因此必须让 DirectXMath 走非 intrinsic 的结构体实现
        target_compile_definitions(Framework PUBLIC _XM_NO_INTRINSICS_)
    else()
        message(FATAL_ERROR "缺少官方 DirectXMath：${LB_DIRECTXMATH_INC}/DirectXMath.h（见 docs/assessment-plan.md M3 节）")
    endif()
    target_link_libraries(Framework PUBLIC lb_glew opengl32)
    # SDK 源码把 wglGetProcAddress 的 PROC 返回值直接赋给 void*（MSVC 宽松、
    # GCC 报错），第三方代码不便改动，这里局部放宽该转换
    if(NOT MSVC)
        target_compile_options(Framework PRIVATE -fpermissive)
    endif()
    message(STATUS "Live2D Cubism SDK: ${LB_CUBISM_SDK_DIR}（Framework + OpenGL 后端 + GLEW）")

    # SDK 接入关卡测试：MinGW 链接 + Framework 启动
    add_executable(lb_cubism_link_test tools/cubism_link_test.cpp)
    target_include_directories(lb_cubism_link_test PRIVATE
        "${LB_CUBISM_SDK_DIR}/Core/include"
        "${LB_CUBISM_SDK_DIR}/Framework/src")
    target_link_libraries(lb_cubism_link_test PRIVATE Framework "${LB_CUBISM_CORE_IMPLIB}")
    target_compile_features(lb_cubism_link_test PRIVATE cxx_std_17)
    add_custom_command(TARGET lb_cubism_link_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LB_CUBISM_CORE_DLL}" "$<TARGET_FILE_DIR:lb_cubism_link_test>/Live2DCubismCore.dll")
endif()

# 核心层：平台无关的接口与纯逻辑
add_subdirectory(core)

# 平台适配层：Windows 实现（采集 / 虚拟摄像头 / 浮窗 / GPU / 渲染 / 推理客户端）
add_subdirectory(platform/windows)

# 应用层：Qt6 桌面界面（黑色主题，无表情符号，按 docs/assessment-plan.md 第七节）
if(LB_BUILD_UI)
    find_package(Qt6 QUIET COMPONENTS Widgets OpenGL OpenGLWidgets)
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

    # 只读检查虚拟摄像头共享内存状态（是否在推帧、画面采样）
    add_executable(lb_vcam_inspect tools/vcam_inspect.cpp)
    target_link_libraries(lb_vcam_inspect PRIVATE lb_core)
    target_compile_features(lb_vcam_inspect PRIVATE cxx_std_17)
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