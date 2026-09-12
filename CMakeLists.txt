cmake_minimum_required(VERSION 3.22)

project(EventHorizon VERSION 0.9.15 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(EVENT_HORIZON_JUCE_PATH "" CACHE PATH "Optional path to a local JUCE checkout")

if (EVENT_HORIZON_JUCE_PATH)
    add_subdirectory("${EVENT_HORIZON_JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.1
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

set(EVENT_HORIZON_FORMATS Standalone VST3)
if (APPLE)
    list(APPEND EVENT_HORIZON_FORMATS AU)
endif()

juce_add_binary_data(EventHorizonAssets
    HEADER_NAME EventHorizonAssets.h
    NAMESPACE EventHorizonAssets
    SOURCES Assets/knob.png)

juce_add_plugin(EventHorizon
    VERSION ${PROJECT_VERSION}
    COMPANY_NAME "Monke Mountain Studios"
    BUNDLE_ID "com.monkemountainstudios.eventhorizon"
    ICON_BIG "Assets/ChatGPT Image 12 sep. 2026 21_03_40.png"
    ICON_SMALL "Assets/ChatGPT Image 12 sep. 2026 21_03_40.png"
    PLUGIN_MANUFACTURER_CODE EvHr
    PLUGIN_CODE EvH1
    FORMATS ${EVENT_HORIZON_FORMATS}
    PRODUCT_NAME "Monke Mountain Event Horizon"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(EventHorizon PRIVATE
    Source/EventHorizonEngine.cpp
    Source/EventHorizonEngine.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h)

target_compile_definitions(EventHorizon PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(EventHorizon
    PRIVATE
        EventHorizonAssets
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

juce_generate_juce_header(EventHorizon)
