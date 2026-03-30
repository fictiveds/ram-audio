cmake_minimum_required(VERSION 3.16)

project(ram_audio_cpp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(ram_audio
    src/main.cpp
    src/algorithms.cpp
    src/ram_audio_engine.cpp
    src/audio_io.cpp
)

target_include_directories(ram_audio PRIVATE src)

if(MSVC)
    target_compile_options(ram_audio PRIVATE /W4)
else()
    target_compile_options(ram_audio PRIVATE -Wall -Wextra -Wpedantic)
endif()
