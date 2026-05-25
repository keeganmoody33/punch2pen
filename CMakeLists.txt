cmake_minimum_required(VERSION 3.20)
project(punch2pen VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# output to bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Shared include directory
# Shared include directory
include_directories(shared)

add_subdirectory(engine)

option(PUNCH2PEN_BUILD_PLUGIN "Build JUCE plugin targets" OFF)

if(PUNCH2PEN_BUILD_PLUGIN)
    # Fetch JUCE
    include(FetchContent)
    FetchContent_Declare(
        JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        develop
    )
    FetchContent_MakeAvailable(JUCE)

    add_subdirectory(plugin)

    # Add JUCE AudioPluginHost
    # We check if the directory exists first to avoid errors if the source isn't fully downloaded yet,
    # but FetchContent should handle it.
    add_subdirectory(${juce_SOURCE_DIR}/extras/AudioPluginHost AudioPluginHost)
endif()
