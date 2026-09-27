set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(lb_ui
    src/main.cpp
    src/main_window.h
    src/main_window.cpp
    src/floating/float_window.h
    src/floating/float_window.cpp
    src/pages/models_page.h
    src/pages/models_page.cpp
    src/pages/mocap_page.h
    src/pages/mocap_page.cpp
    src/pages/output_page.h
    src/pages/output_page.cpp
    src/pages/settings_page.h
    src/pages/settings_page.cpp
    src/render/avatar_painter.h
    src/render/avatar_painter.cpp
    src/render/avatar_renderer.h
    src/render/avatar_renderer.cpp
    src/render/cubism_avatar_renderer.h
    src/render/cubism_avatar_renderer.cpp
    src/render/cubism_runtime.h
    src/render/cubism_runtime.cpp
    src/render/live2d_model_library.h
    src/render/live2d_model_library.cpp
    src/render/vcam_compositor.h
    src/render/vcam_compositor.cpp
    src/render/cubism_render_surface.h
    src/render/cubism_render_surface.cpp
    src/widgets/toggle_switch.h
    src/widgets/toggle_switch.cpp
    src/widgets/slider_row.h
    src/widgets/slider_row.cpp
    resources/resources.qrc
)

target_include_directories(lb_ui PRIVATE src)
target_link_libraries(lb_ui PRIVATE Qt6::Widgets lb_core lb_win)

# Live2D Cubism：Framework（官方 CMake 已 add_subdirectory）+ Core 导入库
if(LB_WITH_CUBISM)
    target_compile_definitions(lb_ui PRIVATE LB_WITH_CUBISM)
    target_include_directories(lb_ui PRIVATE
        "${LB_CUBISM_SDK_DIR}/Core/include"
        "${LB_CUBISM_SDK_DIR}/Framework/src")
    target_link_libraries(lb_ui PRIVATE Framework "${LB_CUBISM_CORE_IMPLIB}")
if(LB_WITH_CUBISM)
    target_link_libraries(lb_ui PRIVATE Qt6::OpenGL Qt6::OpenGLWidgets)
endif()
    # 运行时需要官方 Core dll 与 Cubism 的 GLSL 源码（渲染器按相对路径
    # FrameworkShaders/ 加载），两者都必须与 exe 同目录
    add_custom_command(TARGET lb_ui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LB_CUBISM_CORE_DLL}" "$<TARGET_FILE_DIR:lb_ui>/Live2DCubismCore.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${LB_CUBISM_SDK_DIR}/Framework/src/Rendering/OpenGL/Shaders/Standard"
                "$<TARGET_FILE_DIR:lb_ui>/FrameworkShaders"
        COMMENT "Copying Live2DCubismCore.dll and Cubism shaders next to lb_ui.exe")
endif()

# 合成器离屏验证（不需 UI/相机/设备）：背景虚化 + Live2D 叠加 + 两种内容模式
option(LB_BUILD_VCAM_COMPOSE_TEST "Build vcam compositor offscreen test" OFF)
if(LB_BUILD_VCAM_COMPOSE_TEST)
    add_executable(lb_vcam_compose_test
        ${CMAKE_SOURCE_DIR}/tools/vcam_compose_test.cpp
        src/render/avatar_painter.cpp
        src/render/vcam_compositor.cpp
    )
    target_include_directories(lb_vcam_compose_test PRIVATE src)
    target_link_libraries(lb_vcam_compose_test PRIVATE Qt6::Gui lb_core)
    target_compile_features(lb_vcam_compose_test PRIVATE cxx_std_17)
endif()

# 模型库/参数映射验证（不需要 Cubism SDK）
option(LB_BUILD_LIVE2D_TEST "Build live2d model library + param map test" OFF)
if(LB_BUILD_LIVE2D_TEST)
    add_executable(lb_live2d_model_test
        ${CMAKE_SOURCE_DIR}/tools/live2d_model_test.cpp
        src/render/live2d_model_library.cpp
    )
    target_include_directories(lb_live2d_model_test PRIVATE src)
    target_link_libraries(lb_live2d_model_test PRIVATE Qt6::Core lb_core)
    target_compile_features(lb_live2d_model_test PRIVATE cxx_std_17)
endif()

# Cubism 离屏渲染验证（需要 SDK + 离屏 GL 上下文）
option(LB_BUILD_CUBISM_TEST "Build cubism offscreen render test" OFF)
if(LB_BUILD_CUBISM_TEST AND LB_WITH_CUBISM)
    add_executable(lb_cubism_model_test ${CMAKE_SOURCE_DIR}/tools/cubism_model_test.cpp)
    target_include_directories(lb_cubism_model_test PRIVATE
        "${LB_CUBISM_SDK_DIR}/Core/include"
        "${LB_CUBISM_SDK_DIR}/Framework/src")
    target_link_libraries(lb_cubism_model_test PRIVATE
        Qt6::Gui Qt6::OpenGL Framework "${LB_CUBISM_CORE_IMPLIB}")
    target_compile_features(lb_cubism_model_test PRIVATE cxx_std_17)
    add_custom_command(TARGET lb_cubism_model_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LB_CUBISM_CORE_DLL}" "$<TARGET_FILE_DIR:lb_cubism_model_test>/Live2DCubismCore.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${LB_CUBISM_SDK_DIR}/Framework/src/Rendering/OpenGL/Shaders/Standard"
                "$<TARGET_FILE_DIR:lb_cubism_model_test>/FrameworkShaders")

    # 渲染器级验证：CubismAvatarRenderer 的完整接口路径（tick + render + 合成）
    add_executable(lb_cubism_renderer_test
        ${CMAKE_SOURCE_DIR}/tools/cubism_renderer_test.cpp
        src/render/avatar_painter.cpp
        src/render/avatar_renderer.cpp
        src/render/cubism_avatar_renderer.cpp
        src/render/cubism_runtime.cpp
        src/render/cubism_render_surface.cpp
        src/render/live2d_model_library.cpp
        src/render/vcam_compositor.cpp
    )
    target_compile_definitions(lb_cubism_renderer_test PRIVATE LB_WITH_CUBISM)
    target_include_directories(lb_cubism_renderer_test PRIVATE
        src
        "${LB_CUBISM_SDK_DIR}/Core/include"
        "${LB_CUBISM_SDK_DIR}/Framework/src")
    target_link_libraries(lb_cubism_renderer_test PRIVATE
        Qt6::Gui Qt6::OpenGL Qt6::OpenGLWidgets Framework "${LB_CUBISM_CORE_IMPLIB}" lb_core)
    target_compile_features(lb_cubism_renderer_test PRIVATE cxx_std_17)
    if(NOT MSVC)
        target_compile_options(lb_cubism_renderer_test PRIVATE -fpermissive)
    endif()
    # 原生 WGL 上下文探测（绕过 Qt 格式协商）
if(LB_BUILD_GL_PROBE)
    add_executable(lb_wgl_probe ${CMAKE_SOURCE_DIR}/tools/wgl_probe.cpp)
    target_include_directories(lb_wgl_probe PRIVATE "${LB_GLEW_DIR}/include")
    target_compile_definitions(lb_wgl_probe PRIVATE GLEW_STATIC)
    target_link_libraries(lb_wgl_probe PRIVATE lb_glew opengl32)
    target_compile_features(lb_wgl_probe PRIVATE cxx_std_17)
endif()

# GL 上下文格式探测（排查 Core/Compatibility Profile 问题）
option(LB_BUILD_GL_PROBE "Build GL format probe" OFF)
if(LB_BUILD_GL_PROBE)
    add_executable(lb_gl_format_probe ${CMAKE_SOURCE_DIR}/tools/gl_format_probe.cpp)
    target_link_libraries(lb_gl_format_probe PRIVATE Qt6::Gui)
    target_compile_features(lb_gl_format_probe PRIVATE cxx_std_17)
endif()

# 模型参数名 dump（确认手臂/手部参数命名）
    add_executable(lb_cubism_params_dump
        ${CMAKE_SOURCE_DIR}/tools/cubism_params_dump.cpp
        src/render/cubism_runtime.cpp
    )
    target_compile_definitions(lb_cubism_params_dump PRIVATE LB_WITH_CUBISM)
    target_include_directories(lb_cubism_params_dump PRIVATE
        src
        "${LB_CUBISM_SDK_DIR}/Core/include"
        "${LB_CUBISM_SDK_DIR}/Framework/src")
    target_link_libraries(lb_cubism_params_dump PRIVATE
        Qt6::Gui Qt6::OpenGL Framework "${LB_CUBISM_CORE_IMPLIB}" lb_core)
    target_compile_features(lb_cubism_params_dump PRIVATE cxx_std_17)
    if(NOT MSVC)
        target_compile_options(lb_cubism_params_dump PRIVATE -fpermissive)
    endif()
    add_custom_command(TARGET lb_cubism_params_dump POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LB_CUBISM_CORE_DLL}" "$<TARGET_FILE_DIR:lb_cubism_params_dump>/Live2DCubismCore.dll")

    add_custom_command(TARGET lb_cubism_renderer_test POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${LB_CUBISM_CORE_DLL}" "$<TARGET_FILE_DIR:lb_cubism_renderer_test>/Live2DCubismCore.dll"
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${LB_CUBISM_SDK_DIR}/Framework/src/Rendering/OpenGL/Shaders/Standard"
                "$<TARGET_FILE_DIR:lb_cubism_renderer_test>/FrameworkShaders")
endif()
