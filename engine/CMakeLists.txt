cmake_minimum_required(VERSION 3.20)

add_executable(punch2penEngine
    src/main.cpp
    src/IPCServer.cpp
    src/OpenAICloudTranscriber.cpp
    src/TranscriptionCoordinator.cpp
    src/Transcriber.cpp
    src/DatabaseManager.cpp
    src/ProfileManager.cpp
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

FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(nlohmann_json)

set(USE_TLS ON CACHE BOOL "Enable TLS for ixwebsocket" FORCE)
FetchContent_Declare(
    ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG        v11.4.5
)
FetchContent_MakeAvailable(ixwebsocket)

if(APPLE)
    find_library(ACCELERATE_FRAMEWORK Accelerate)
    target_link_libraries(punch2penEngine PRIVATE ${ACCELERATE_FRAMEWORK})
endif()

target_link_libraries(punch2penEngine PRIVATE whisper nlohmann_json::nlohmann_json ixwebsocket)

set_target_properties(punch2penEngine PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

add_executable(transcriptionCoordinatorTest
    tests/TranscriptionCoordinatorTest.cpp
    src/TranscriptionCoordinator.cpp
    src/DatabaseManager.cpp
    src/ProfileManager.cpp
)

target_include_directories(transcriptionCoordinatorTest
    PRIVATE src
    PRIVATE ../shared
)

target_link_libraries(transcriptionCoordinatorTest PRIVATE nlohmann_json::nlohmann_json)

set_target_properties(transcriptionCoordinatorTest PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

add_executable(databaseManagerTest
    tests/DatabaseManagerTest.cpp
    src/DatabaseManager.cpp
)

target_include_directories(databaseManagerTest
    PRIVATE src
)

set_target_properties(databaseManagerTest PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

add_executable(profileManagerTest
    tests/ProfileManagerTest.cpp
    src/ProfileManager.cpp
)

target_include_directories(profileManagerTest
    PRIVATE src
    PRIVATE ../shared
)

target_link_libraries(profileManagerTest PRIVATE nlohmann_json::nlohmann_json)

set_target_properties(profileManagerTest PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

add_executable(protocolSerializationTest
    tests/ProtocolSerializationTest.cpp
)

target_include_directories(protocolSerializationTest
    PRIVATE src
    PRIVATE ../shared
)

set_target_properties(protocolSerializationTest PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

add_executable(openAIJsonTest
    tests/OpenAIJsonTest.cpp
)

target_include_directories(openAIJsonTest
    PRIVATE src
    PRIVATE ../shared
)

target_link_libraries(openAIJsonTest PRIVATE nlohmann_json::nlohmann_json)

set_target_properties(openAIJsonTest PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
