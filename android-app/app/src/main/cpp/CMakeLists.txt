cmake_minimum_required(VERSION 3.22.1)
project(micify_native)

find_package(oboe REQUIRED CONFIG)

# libopus is not distributed by the NDK, so it's vendored as source under
# third_party/opus and built from there. See android-app/README.md for the
# one-time setup command (git submodule / clone).
set(OPUS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/opus")
if(NOT EXISTS "${OPUS_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "libopus source not found at ${OPUS_DIR}.\n"
        "Run (from the repo root):\n"
        "  git clone --depth 1 --branch v1.5.2 https://github.com/xiph/opus android-app/app/src/main/cpp/third_party/opus\n"
        "See android-app/README.md.")
endif()

set(OPUS_BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(OPUS_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
add_subdirectory(${OPUS_DIR} opus_build)

add_library(micify_native SHARED
    native_bridge.cpp
    streamer.cpp
)

target_include_directories(micify_native PRIVATE
    ${OPUS_DIR}/include
)

target_link_libraries(micify_native
    oboe::oboe
    opus
    android
    log
)

target_compile_options(micify_native PRIVATE -Wall -Wextra)
