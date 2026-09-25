set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

add_executable(lb_ui
    src/main.cpp
    src/main_window.h
    src/main_window.cpp
    src/pages/models_page.h
    src/pages/models_page.cpp
    src/pages/mocap_page.h
    src/pages/mocap_page.cpp
    src/pages/output_page.h
    src/pages/output_page.cpp
    src/pages/settings_page.h
    src/pages/settings_page.cpp
    src/widgets/toggle_switch.h
    src/widgets/toggle_switch.cpp
    src/widgets/slider_row.h
    src/widgets/slider_row.cpp
    resources/resources.qrc
)

target_include_directories(lb_ui PRIVATE src)
target_link_libraries(lb_ui PRIVATE Qt6::Widgets lb_core lb_win)