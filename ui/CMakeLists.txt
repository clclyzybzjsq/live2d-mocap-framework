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
    src/render/live2d_model_library.h
    src/render/live2d_model_library.cpp
    src/render/vcam_compositor.h
    src/render/vcam_compositor.cpp
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
endif()