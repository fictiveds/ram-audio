cmake_minimum_required(VERSION 3.16)

project(ram_audio_cpp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

add_executable(ram_audio
    src/main.cpp
    src/algorithms/core_registry.cpp
    src/algorithms/classic_algorithms.cpp
    src/algorithms/texture_algorithms.cpp
    src/algorithms/advanced_algorithms.cpp
    src/algorithms/musical_algorithms.cpp
    src/algorithms/experimental_algorithms.cpp
    src/telemetry.cpp
    src/ram_audio_engine.cpp
    src/audio_io.cpp
)

target_include_directories(ram_audio PRIVATE src)

if(MSVC)
    target_compile_options(ram_audio PRIVATE /W4)
else()
    target_compile_options(ram_audio PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_executable(ram_audio_telemetry_test
    tests/telemetry_test.cpp
    src/telemetry.cpp
)

target_include_directories(ram_audio_telemetry_test PRIVATE src)

if(MSVC)
    target_compile_options(ram_audio_telemetry_test PRIVATE /W4)
else()
    target_compile_options(ram_audio_telemetry_test PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME telemetry_ranges COMMAND ram_audio_telemetry_test)
