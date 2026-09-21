cmake_minimum_required(VERSION 3.22)

# Paid profile API (Convex HTTP actions under cloud/). Empty means the build
# has no sign-in: the plugin stays free/lite and the engine never opens a
# socket to anything but 127.0.0.1:7483. Release builds pass the deployed
# `.convex.site` URL; a Mac install can also drop ~/.punch2pen/profile-api.json.
set(PUNCH2PEN_PROFILE_API_URL "" CACHE STRING
    "Base URL of the punch2pen profile API compiled into punch2penEngine")

add_executable(punch2penEngine
    src/main.cpp
    src/IPCServer.cpp
    src/OpenAICloudTranscriber.cpp
    src/TranscriptionCoordinator.cpp
    src/Transcriber.cpp
    src/Dictionary.cpp
    src/CloudProfileClient.cpp
    src/IxHttpTransport.cpp
    src/AccountManager.cpp
)

target_include_directories(punch2penEngine
    PRIVATE ../shared
)

target_compile_definitions(punch2penEngine PRIVATE
    PUNCH2PEN_PROFILE_API_URL_DEFAULT="${PUNCH2PEN_PROFILE_API_URL}"
)

# whisper.cpp defaults BUILD_SHARED_LIBS ON (non-MINGW). That produced
# @rpath/libwhisper.1.dylib + ggml/ixwebsocket dylibs with LC_RPATH set to
# the CI/build machine (build/lib). The 1.0.0 pkg shipped only the engine
# binary, so dyld aborted before main() and nothing bound 127.0.0.1:7483.
# Static-link the inference/websocket stack; remaining dylibs are bundled
# next to the binary with @executable_path / @loader_path (see POST_BUILD).
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Static whisper/ggml/ixwebsocket" FORCE)
set(GGML_BACKEND_DL OFF CACHE BOOL "" FORCE)
set(GGML_OPENMP OFF CACHE BOOL "" FORCE)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(APPLE)
    set(GGML_METAL_EMBED_LIBRARY ON CACHE BOOL "" FORCE)
    set(USE_OPEN_SSL OFF CACHE BOOL "" FORCE)
    set(USE_MBED_TLS OFF CACHE BOOL "" FORCE)
    set(CMAKE_INSTALL_RPATH_USE_LINK_PATH OFF)
    set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)
    set(CMAKE_MACOSX_RPATH ON)
endif()

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

if(APPLE)
    set_target_properties(punch2penEngine PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        BUILD_RPATH "@executable_path;@loader_path"
        INSTALL_RPATH "@executable_path;@loader_path"
        INSTALL_RPATH_USE_LINK_PATH FALSE
        MACOSX_RPATH TRUE
    )
    add_custom_command(TARGET punch2penEngine POST_BUILD
        COMMAND /bin/bash
                "${CMAKE_SOURCE_DIR}/installer/macos/bundle_engine_libs.sh"
                "$<TARGET_FILE:punch2penEngine>"
                "${CMAKE_BINARY_DIR}/lib"
        COMMAND /bin/bash
                "${CMAKE_SOURCE_DIR}/scripts/check_engine_rpath.sh"
                "$<TARGET_FILE:punch2penEngine>"
        COMMENT "Rewrite punch2penEngine rpath; bundle remaining dylibs"
    )
endif()

add_executable(transcriptionCoordinatorTest
    tests/TranscriptionCoordinatorTest.cpp
    src/TranscriptionCoordinator.cpp
    src/Dictionary.cpp
    src/CloudProfileClient.cpp
    src/AccountManager.cpp
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

add_executable(dictionaryTest
    tests/DictionaryTest.cpp
    src/Dictionary.cpp
)

target_include_directories(dictionaryTest
    PRIVATE src
)

target_link_libraries(dictionaryTest PRIVATE nlohmann_json::nlohmann_json)

set_target_properties(dictionaryTest PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# Free stays local, login flips to paid, corrections write the profile
# dictionary, seats stay isolated. Mock profile API, no network.
add_executable(accountManagerTest
    tests/AccountManagerTest.cpp
    src/AccountManager.cpp
    src/Dictionary.cpp
    src/CloudProfileClient.cpp
)

target_include_directories(accountManagerTest
    PRIVATE src
    PRIVATE ../shared
)

target_link_libraries(accountManagerTest PRIVATE nlohmann_json::nlohmann_json)

set_target_properties(accountManagerTest PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)

# HTTP contract against cloud/convex/http.ts with a fake transport.
add_executable(cloudProfileClientTest
    tests/CloudProfileClientTest.cpp
    src/CloudProfileClient.cpp
    src/Dictionary.cpp
)

target_include_directories(cloudProfileClientTest
    PRIVATE src
    PRIVATE ../shared
)

target_link_libraries(cloudProfileClientTest PRIVATE nlohmann_json::nlohmann_json)

set_target_properties(cloudProfileClientTest PROPERTIES
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

add_executable(transcriptTimingTest
    tests/TranscriptTimingTest.cpp
)

target_include_directories(transcriptTimingTest
    PRIVATE src
)

set_target_properties(transcriptTimingTest PROPERTIES
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
