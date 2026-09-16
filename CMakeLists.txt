cmake_minimum_required(VERSION 3.22)
project(punch2pen VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

include_directories(shared)

add_subdirectory(engine)

option(PUNCH2PEN_BUILD_PLUGIN "Build JUCE plugin targets" OFF)

if(PUNCH2PEN_BUILD_PLUGIN)
    include(FetchContent)
    # Newest JUCE as of 2026-09-16: develop @ c07f901 (15 Sep) is ahead of
    # the latest release tag 9.0.2 (7 Sep). Pin the SHA so FetchContent is
    # reproducible instead of tracking a floating branch.
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        c07f901f014e6df6431778f62ad0ce681a444c37
    )
    FetchContent_MakeAvailable(JUCE)

    add_subdirectory(plugin)
    add_subdirectory(${juce_SOURCE_DIR}/extras/AudioPluginHost AudioPluginHost)
endif()
