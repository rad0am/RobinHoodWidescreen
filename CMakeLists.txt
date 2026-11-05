cmake_minimum_required(VERSION 3.20)
project(RobinHoodWidescreen CXX)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Wrong architecture: build as x86 (32-bit)!")
endif()

add_library(RobinHoodWidescreen SHARED
    src/main.cpp
    src/dinput_proxy.cpp
    src/crash_reporter.cpp
    src/core/log.cpp
    src/core/patch.cpp
    src/features/bar_fill.cpp
    src/features/camera.cpp
    src/features/dialog_scaling.cpp
    src/features/menu_labels.cpp
    src/features/resolution.cpp
    exports.def
)

target_include_directories(RobinHoodWidescreen PRIVATE src)

set_target_properties(RobinHoodWidescreen PROPERTIES OUTPUT_NAME dinput)
