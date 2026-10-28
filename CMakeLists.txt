cmake_minimum_required(VERSION 3.20)
project(RobinHoodWidescreen CXX)

set(CMAKE_CXX_STANDARD 20)

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "Wrong architecture: build as x86 (32-bit)!")
endif()

add_library(RobinHoodWidescreen SHARED dllmain.cpp exports.def)

set_target_properties(RobinHoodWidescreen PROPERTIES OUTPUT_NAME dinput)
