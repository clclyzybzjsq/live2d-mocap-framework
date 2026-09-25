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

# ---- DirectShow 虚拟摄像头 filter（独立 DLL，静态运行时） ----
add_library(lb_vcam_filter SHARED
    vcam_filter/vcam_filter.cpp
    vcam_filter/vcam_source.cpp
)
set_target_properties(lb_vcam_filter PROPERTIES PREFIX "")
target_include_directories(lb_vcam_filter PRIVATE
    include
    ${CMAKE_SOURCE_DIR}/core/include
)
target_link_libraries(lb_vcam_filter PRIVATE
    strmiids ole32 oleaut32 uuid
)
target_compile_features(lb_vcam_filter PRIVATE cxx_std_17)
if(NOT MSVC)
    target_compile_options(lb_vcam_filter PRIVATE -Wall -Wextra)
    # filter 运行在消费进程（OBS/Teams/浏览器），必须全静态运行时
    target_link_options(lb_vcam_filter PRIVATE
        -static
        # 只导出显式标记的 5 个入口（DllMain/DllGetClassObject/...），
        # 避免导出表污染消费进程的符号绑定
        "-Wl,--exclude-all-symbols")
endif()

# 冒烟/开发时把 filter DLL 与 UI exe 放同一目录，保证输出页可加载注册
if(TARGET lb_ui)
    add_custom_command(TARGET lb_ui POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:lb_vcam_filter>
            $<TARGET_FILE_DIR:lb_ui>/lb_vcam_filter.dll
        COMMENT "Copying lb_vcam_filter.dll next to lb_ui.exe"
    )
endif()