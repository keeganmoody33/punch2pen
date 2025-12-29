cmake_minimum_required(VERSION 3.20)

add_executable(punch2penEngine
    src/main.cpp
    src/IPCServer.cpp
    src/Transcriber.cpp
    src/DatabaseManager.cpp
    src/DictionaryProcessor.cpp
)

target_include_directories(punch2penEngine
    PRIVATE ../shared
)


# Fetch whisper.cpp
include(FetchContent)
FetchContent_Declare(
    whisper
    GIT_REPOSITORY https://github.com/ggerganov/whisper.cpp.git
    GIT_TAG        v1.7.4
)
# We only want the library, not the examples
set(WHISPER_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(WHISPER_BUILD_SERVER OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(whisper)

if(APPLE)
    find_library(ACCELERATE_FRAMEWORK Accelerate)
    target_link_libraries(punch2penEngine PRIVATE ${ACCELERATE_FRAMEWORK})
endif()

target_link_libraries(punch2penEngine PRIVATE whisper)

set_target_properties(punch2penEngine PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
