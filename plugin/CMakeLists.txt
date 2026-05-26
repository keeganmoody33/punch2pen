cmake_minimum_required(VERSION 3.20)



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

# ---------- Plugin test targets ----------

# RingBuffer test (header-only, needs juce_core + juce_audio_basics)
juce_add_console_app(ringBufferTest PRODUCT_NAME "ringBufferTest")
juce_generate_juce_header(ringBufferTest)
target_sources(ringBufferTest PRIVATE tests/RingBufferTest.cpp)
target_link_libraries(ringBufferTest PRIVATE
    juce::juce_core
    juce::juce_audio_basics
)
target_include_directories(ringBufferTest PRIVATE
    Source
    ${CMAKE_SOURCE_DIR}/shared
)

# IPCClient test (needs socket + thread support from juce_core/juce_events)
juce_add_console_app(ipcClientTest PRODUCT_NAME "ipcClientTest")
juce_generate_juce_header(ipcClientTest)
target_sources(ipcClientTest PRIVATE
    tests/IPCClientTest.cpp
    Source/IPCClient.cpp
)
target_link_libraries(ipcClientTest PRIVATE
    juce::juce_core
    juce::juce_audio_basics
    juce::juce_events
    juce::juce_gui_basics
)
target_include_directories(ipcClientTest PRIVATE
    Source
    ${CMAKE_SOURCE_DIR}/shared
)

# PluginProcessor state test (requires full plugin sources)
juce_add_console_app(pluginProcessorStateTest PRODUCT_NAME "pluginProcessorStateTest")
juce_generate_juce_header(pluginProcessorStateTest)
target_sources(pluginProcessorStateTest PRIVATE
    tests/PluginProcessorStateTest.cpp
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/TranscriptView.cpp
    Source/CorrectionEditor.cpp
    Source/PositionDisplay.cpp
    Source/IPCClient.cpp
)
target_link_libraries(pluginProcessorStateTest PRIVATE
    juce::juce_audio_utils
    juce::juce_recommended_config_flags
)
target_include_directories(pluginProcessorStateTest PRIVATE
    Source
    ${CMAKE_SOURCE_DIR}/shared
)
target_compile_definitions(pluginProcessorStateTest PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JucePlugin_Name="punch2pen"
    JucePlugin_WantsMidiInput=0
    JucePlugin_ProducesMidiOutput=0
    JucePlugin_IsMidiEffect=0
    JucePlugin_IsSynth=0
)
