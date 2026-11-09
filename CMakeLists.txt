cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

project(MasterVerb VERSION 1.2.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.9
    GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(JUCE)

if(APPLE)
    set(MAS_FORMATS AU VST3)
else()
    set(MAS_FORMATS VST3)
endif()

juce_add_plugin(MasterVerb
    PRODUCT_NAME                "MASTER VERB"
    COMPANY_NAME                "masterastakhovsound"
    BUNDLE_ID                   com.masterastakhovsound.masterverb
    PLUGIN_MANUFACTURER_CODE    Mast
    PLUGIN_CODE                 MVrb
    DESCRIPTION                 "Algorithmic hall reverb"
    FORMATS                     ${MAS_FORMATS}
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    AU_MAIN_TYPE                kAudioUnitType_Effect
    VST3_CATEGORIES             Fx Reverb)

juce_add_binary_data(MasterVerbData SOURCES
    Resources/emblem.png
    Resources/wordmark.png)

target_sources(MasterVerb PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(MasterVerb PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(MasterVerb
    PRIVATE
        MasterVerbData
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
