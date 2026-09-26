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