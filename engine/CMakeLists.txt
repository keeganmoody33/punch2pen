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

set_target_properties(punch2penEngine PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
)
