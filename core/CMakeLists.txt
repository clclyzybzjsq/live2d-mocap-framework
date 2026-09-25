add_library(lb_core STATIC
    src/smoothing.cpp
    src/mapping.cpp
    src/solve.cpp
    src/motion_pipeline.cpp
)

target_include_directories(lb_core PUBLIC include)
target_compile_features(lb_core PUBLIC cxx_std_17)

if(MSVC)
    target_compile_options(lb_core PRIVATE /W4)
else()
    target_compile_options(lb_core PRIVATE -Wall -Wextra)
endif()