cmake_minimum_required(VERSION 3.22)

set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "Minimum macOS version")

project(MasAudio VERSION 1.0.0 LANGUAGES C CXX)

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

juce_add_binary_data(MasBrandData SOURCES
    Resources/emblem.png
    Resources/wordmark.png)

# mas_add_plugin(<cmake target> <product name> <4-char AU code> <bundle id> <version> <source dir> <description>)
function(mas_add_plugin target product code bundle version srcdir description)
    juce_add_plugin(${target}
        PRODUCT_NAME                "${product}"
        VERSION                     ${version}
        COMPANY_NAME                "masterastakhovsound"
        BUNDLE_ID                   ${bundle}
        PLUGIN_MANUFACTURER_CODE    Mast
        PLUGIN_CODE                 ${code}
        DESCRIPTION                 "${description}"
        FORMATS                     ${MAS_FORMATS}
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS FALSE
        COPY_PLUGIN_AFTER_BUILD     FALSE
        AU_MAIN_TYPE                kAudioUnitType_Effect
        VST3_CATEGORIES             Fx Reverb)

    target_sources(${target} PRIVATE
        ${srcdir}/PluginProcessor.cpp
        ${srcdir}/PluginEditor.cpp)

    target_compile_definitions(${target} PUBLIC
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_VST3_CAN_REPLACE_VST2=0)

    target_link_libraries(${target}
        PRIVATE
            MasBrandData
            juce::juce_audio_utils
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)
endfunction()

# MASTER SPACE: spatial engine (Spatial DNA + Depth + locks), separate product.
mas_add_plugin(MasterSpace "MASTER SPACE" MSpc com.masterastakhovsound.masterspace 1.0.0
               Source/MasterSpace "Spatial reverb: room, depth, early and late")

# MASTER VERB 1.x and its twins ALENTEY VERB, ALEX HOOK VERB (same engine and UI, different name).
mas_add_plugin(MasterVerb  "MASTER VERB"  MVrb com.masterastakhovsound.masterverb 1.2.0
               Source/MasterVerb "Algorithmic hall reverb")
mas_add_plugin(AlenteyVerb "ALENTEY VERB" AVrb com.masterastakhovsound.alenteyverb 1.2.0
               Source/MasterVerb "Algorithmic hall reverb")
mas_add_plugin(AlexHookVerb "ALEX HOOK VERB" AHvb com.masterastakhovsound.alexhookverb 1.2.0
               Source/MasterVerb "Algorithmic hall reverb")
