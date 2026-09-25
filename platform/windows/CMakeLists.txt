add_library(lb_win STATIC
    src/win_capture_source.cpp
    src/win_virtual_camera.cpp
    src/win_floating_window.cpp
    src/win_gpu_context.cpp
    src/win_render_backend.cpp
    src/win_face_tracker.cpp
    src/factory.cpp
)

target_include_directories(lb_win PUBLIC include)
target_link_libraries(lb_win PUBLIC lb_core PRIVATE
    mf mfplat mfreadwrite mfuuid ole32 oleaut32)
target_compile_features(lb_win PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(lb_win PRIVATE /W4)
else()
    target_compile_options(lb_win PRIVATE -Wall -Wextra)
endif()