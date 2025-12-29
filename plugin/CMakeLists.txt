cmake_minimum_required(VERSION 3.20)

# Fetch JUCE
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        develop 
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(punch2pen_plugin
    COMPANY_NAME "Doctaaa"
    PLUGIN_MANUFACTURER_CODE "Dcta"
    PLUGIN_CODE "P2pn"
    FORMATS VST3 AU
    PRODUCT_NAME "punch2pen"
)

juce_generate_juce_header(punch2pen_plugin)

target_sources(punch2pen_plugin
    PRIVATE
        Source/PluginProcessor.cpp
        Source/PluginProcessor.h
        Source/PluginEditor.cpp
        Source/PluginEditor.h
        Source/TranscriptView.cpp
        Source/TranscriptView.h
        Source/IPCClient.cpp
        Source/IPCClient.h
        Source/PositionDisplay.cpp
        Source/PositionDisplay.h
        Source/RingBuffer.h
)

target_include_directories(punch2pen_plugin PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/punch2pen_plugin_artefacts/JuceLibraryCode"
)

target_compile_definitions(punch2pen_plugin PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
)

target_link_libraries(punch2pen_plugin
    PRIVATE
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags
)
