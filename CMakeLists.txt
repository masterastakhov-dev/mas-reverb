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

# MASTER SPACE factory preset library (JSON, one file per preset). New presets need no DSP code changes.
file(GLOB MAS_SPACE_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/factory/*.json)
juce_add_binary_data(MasterSpacePresets HEADER_NAME MasterSpacePresets.h NAMESPACE MasterSpacePresets
    SOURCES ${MAS_SPACE_PRESETS})

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
mas_add_plugin(MasterSpace "MASTER SPACE" MSpc com.masterastakhovsound.masterspace 1.2.0
               Source/MasterSpace "Spatial reverb: room, depth, early and late")
target_sources(MasterSpace PRIVATE Source/MasterSpace/PresetManager.cpp Source/MasterSpace/PresetPanel.cpp)
target_link_libraries(MasterSpace PRIVATE MasterSpacePresets)

# MASTER SPACE ATMOS: experimental build with the ATMOSPHERIC TAIL late engine (MAS_ATMOS). Same sources as
# MASTER SPACE 1.2, its own name, code, bundle id and preset folder, so it installs next to 1.2 without conflicts.
mas_add_plugin(MasterSpaceAtmos "MASTER SPACE ATMOS" MSpA com.masterastakhovsound.masterspaceatmos 1.3.0
               Source/MasterSpace "MASTER SPACE with the experimental ATMOSPHERIC TAIL engine")
target_sources(MasterSpaceAtmos PRIVATE Source/MasterSpace/PresetManager.cpp Source/MasterSpace/PresetPanel.cpp)
target_compile_definitions(MasterSpaceAtmos PRIVATE MAS_ATMOS=1)
file(GLOB MAS_ATMOS_PRESETS CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/presets/factory-atmos/*.json)
juce_add_binary_data(MasterSpaceAtmosPresets HEADER_NAME MasterSpacePresets.h NAMESPACE MasterSpacePresets
    SOURCES ${MAS_SPACE_PRESETS} ${MAS_ATMOS_PRESETS})
target_link_libraries(MasterSpaceAtmos PRIVATE MasterSpaceAtmosPresets)

# MASTER SPACE 1.4: ATMOS 1.3 + REVERB INTELLIGENCE (SMART DUCKER, RESONANCE GUARD; MAS_RI). Same sources,
# its own name, code, bundle id and preset folder: installs next to 1.2 and ATMOS 1.3 without conflicts.
mas_add_plugin(MasterSpace14 "MASTER SPACE 1.4" MSp4 com.masterastakhovsound.masterspace14 1.4.0
               Source/MasterSpace "MASTER SPACE with SMART DUCKER and RESONANCE GUARD")
target_sources(MasterSpace14 PRIVATE Source/MasterSpace/PresetManager.cpp Source/MasterSpace/PresetPanel.cpp)
target_compile_definitions(MasterSpace14 PRIVATE MAS_ATMOS=1 MAS_RI=1)
juce_add_binary_data(MasterSpace14Presets HEADER_NAME MasterSpacePresets.h NAMESPACE MasterSpacePresets
    SOURCES ${MAS_SPACE_PRESETS} ${MAS_ATMOS_PRESETS})
target_link_libraries(MasterSpace14 PRIVATE MasterSpace14Presets)

# MASTER SPACE 1.5 EQ LAB: 1.4 + REVERB EQ (three banks: WET / EARLY / LATE) and the spectral analyzer (MAS_EQ).
# Same sources, its own name, code, bundle id and preset folder: installs next to every earlier build.
mas_add_plugin(MasterSpace15 "MASTER SPACE 1.5 EQ LAB" MSp5 com.masterastakhovsound.masterspace15eqlab 1.5.0
               Source/MasterSpace "MASTER SPACE with REVERB EQ and spectral analyzer")
target_sources(MasterSpace15 PRIVATE Source/MasterSpace/PresetManager.cpp Source/MasterSpace/PresetPanel.cpp)
target_compile_definitions(MasterSpace15 PRIVATE MAS_ATMOS=1 MAS_RI=1 MAS_EQ=1)
juce_add_binary_data(MasterSpace15Presets HEADER_NAME MasterSpacePresets.h NAMESPACE MasterSpacePresets
    SOURCES ${MAS_SPACE_PRESETS} ${MAS_ATMOS_PRESETS})
target_link_libraries(MasterSpace15 PRIVATE MasterSpace15Presets)

# MASTER SPACE STUDIO TEST: the 1.5 EQ LAB engine, unchanged, with the studio redesign of the editor (MAS_STUDIO).
# A test build: its own name, code, bundle id and preset folder, so it installs next to 1.5 and every earlier build.
juce_add_binary_data(MasStudioFonts HEADER_NAME StudioFonts.h NAMESPACE StudioFonts
    SOURCES Resources/fonts/Inter-Regular.ttf Resources/fonts/Inter-SemiBold.ttf Resources/fonts/Cinzel-SemiBold.ttf)
mas_add_plugin(MasterSpaceStudio "MASTER SPACE STUDIO TEST" MSp6 com.masterastakhovsound.masterspacestudiotest 1.6.0
               Source/MasterSpace "MASTER SPACE studio redesign (test build)")
target_sources(MasterSpaceStudio PRIVATE Source/MasterSpace/PresetManager.cpp Source/MasterSpace/PresetPanel.cpp
               Source/MasterSpace/StudioLook.cpp Source/MasterSpace/StudioEditor.cpp)
target_compile_definitions(MasterSpaceStudio PRIVATE MAS_ATMOS=1 MAS_RI=1 MAS_EQ=1 MAS_STUDIO=1)
juce_add_binary_data(MasterSpaceStudioPresets HEADER_NAME MasterSpacePresets.h NAMESPACE MasterSpacePresets
    SOURCES ${MAS_SPACE_PRESETS} ${MAS_ATMOS_PRESETS})
target_link_libraries(MasterSpaceStudio PRIVATE MasterSpaceStudioPresets MasStudioFonts)

# MASTER SPACE 1.7d DYNAMIC TEST: isolated AU/VST3 identity and preset folder;
# same studio GUI with independently opt-in manual dynamic EQ (MAS_STUDIO_DYN).
mas_add_plugin(MasterSpace17d "MASTER SPACE 1.7d DYNAMIC TEST" MS7d com.masterastakhovsound.masterspace17d 1.7.0
               Source/MasterSpace "MASTER SPACE 1.7d manual dynamic resonance cuts (test build)")
target_sources(MasterSpace17d PRIVATE Source/MasterSpace/PresetManager.cpp Source/MasterSpace/PresetPanel.cpp
               Source/MasterSpace/StudioLook.cpp Source/MasterSpace/StudioEditor.cpp)
target_compile_definitions(MasterSpace17d PRIVATE MAS_ATMOS=1 MAS_RI=1 MAS_EQ=1 MAS_STUDIO=1 MAS_STUDIO_DYN=1)
target_link_libraries(MasterSpace17d PRIVATE MasterSpaceStudioPresets MasStudioFonts)


# MASTER SPACE 1.1 frozen (release-1.1 sources, unchanged) under its own name and code, for A/B against 1.2.
mas_add_plugin(MasterSpace11 "MASTER SPACE 1.1" MSp1 com.masterastakhovsound.masterspace11 1.1.0
               Source/MasterSpace11 "MASTER SPACE 1.1 (reference for A/B)")

# MASTER SPACE 1.0 kept unchanged under its own name and code, for A/B against the current version.
mas_add_plugin(MasterSpaceRef "MASTER SPACE 1.0" MSp0 com.masterastakhovsound.masterspace10 1.0.0
               Source/MasterSpaceRef "MASTER SPACE 1.0 (reference for A/B)")

# MASTER VERB 1.x and its twins ALENTEY VERB, ALEX HOOK VERB (same engine and UI, different name).
mas_add_plugin(MasterVerb  "MASTER VERB"  MVrb com.masterastakhovsound.masterverb 1.2.0
               Source/MasterVerb "Algorithmic hall reverb")
mas_add_plugin(AlenteyVerb "ALENTEY VERB" AVrb com.masterastakhovsound.alenteyverb 1.2.0
               Source/MasterVerb "Algorithmic hall reverb")
mas_add_plugin(AlexHookVerb "ALEX HOOK VERB" AHvb com.masterastakhovsound.alexhookverb 1.2.0
               Source/MasterVerb "Algorithmic hall reverb")

# Headless tests of the MASTER SPACE processor (presets, A/B, state, transitions, CPU): -DMAS_BUILD_TESTS=ON
option(MAS_BUILD_TESTS "Build the MASTER SPACE headless tests" OFF)
if(MAS_BUILD_TESTS)
    juce_add_console_app(MasterSpaceTests PRODUCT_NAME "MasterSpaceTests")
    target_sources(MasterSpaceTests PRIVATE
        tests/space_plugin_tests.cpp
        Source/MasterSpace/PluginProcessor.cpp
        Source/MasterSpace/PluginEditor.cpp
        Source/MasterSpace/PresetManager.cpp
        Source/MasterSpace/PresetPanel.cpp)
    target_compile_definitions(MasterSpaceTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
        JucePlugin_Name="MASTER SPACE" JucePlugin_VersionString="1.2.0")
    target_link_libraries(MasterSpaceTests PRIVATE
        MasterSpacePresets MasBrandData juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

    # the same tests on the experimental ATMOS build
    juce_add_console_app(MasterSpaceAtmosTests PRODUCT_NAME "MasterSpaceAtmosTests")
    target_sources(MasterSpaceAtmosTests PRIVATE
        tests/space_plugin_tests.cpp
        Source/MasterSpace/PluginProcessor.cpp
        Source/MasterSpace/PluginEditor.cpp
        Source/MasterSpace/PresetManager.cpp
        Source/MasterSpace/PresetPanel.cpp)
    target_compile_definitions(MasterSpaceAtmosTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1 MAS_ATMOS=1
        JucePlugin_Name="MASTER SPACE ATMOS" JucePlugin_VersionString="1.3.0")
    target_link_libraries(MasterSpaceAtmosTests PRIVATE
        MasterSpaceAtmosPresets MasBrandData juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

    # the same tests on MASTER SPACE 1.4 (plus the 1.4 checks: modules off = ATMOS, latency, sidechain, migration)
    juce_add_console_app(MasterSpace14Tests PRODUCT_NAME "MasterSpace14Tests")
    target_sources(MasterSpace14Tests PRIVATE
        tests/space_plugin_tests.cpp
        Source/MasterSpace/PluginProcessor.cpp
        Source/MasterSpace/PluginEditor.cpp
        Source/MasterSpace/PresetManager.cpp
        Source/MasterSpace/PresetPanel.cpp)
    target_compile_definitions(MasterSpace14Tests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1 MAS_ATMOS=1 MAS_RI=1
        JucePlugin_Name="MASTER SPACE 1.4" JucePlugin_VersionString="1.4.0")
    target_link_libraries(MasterSpace14Tests PRIVATE
        MasterSpace14Presets MasBrandData juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

    # the same tests on MASTER SPACE 1.5 EQ LAB (plus the EQ checks: EQ off = 1.4, response, routing, clicks, state)
    juce_add_console_app(MasterSpace15Tests PRODUCT_NAME "MasterSpace15Tests")
    target_sources(MasterSpace15Tests PRIVATE
        tests/space_plugin_tests.cpp
        Source/MasterSpace/PluginProcessor.cpp
        Source/MasterSpace/PluginEditor.cpp
        Source/MasterSpace/PresetManager.cpp
        Source/MasterSpace/PresetPanel.cpp)
    target_compile_definitions(MasterSpace15Tests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1 MAS_ATMOS=1 MAS_RI=1 MAS_EQ=1
        JucePlugin_Name="MASTER SPACE 1.5 EQ LAB" JucePlugin_VersionString="1.5.0")
    target_link_libraries(MasterSpace15Tests PRIVATE
        MasterSpace15Presets MasBrandData juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

    # the same tests on the STUDIO TEST build (1.5 engine + studio editor), plus studio snapshots
    juce_add_console_app(MasterSpaceStudioTests PRODUCT_NAME "MasterSpaceStudioTests")
    target_sources(MasterSpaceStudioTests PRIVATE
        tests/space_plugin_tests.cpp
        Source/MasterSpace/PluginProcessor.cpp
        Source/MasterSpace/PluginEditor.cpp
        Source/MasterSpace/PresetManager.cpp
        Source/MasterSpace/PresetPanel.cpp
        Source/MasterSpace/StudioLook.cpp
        Source/MasterSpace/StudioEditor.cpp)
    target_compile_definitions(MasterSpaceStudioTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1 MAS_ATMOS=1 MAS_RI=1 MAS_EQ=1 MAS_STUDIO=1
        JucePlugin_Name="MASTER SPACE STUDIO TEST" JucePlugin_VersionString="1.6.0")
    target_link_libraries(MasterSpaceStudioTests PRIVATE
        MasterSpaceStudioPresets MasStudioFonts MasBrandData juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

    # Separate 1.7d tests: same snapshot coverage + manual dynamic band / peak hold tests.
    juce_add_console_app(MasterSpace17dTests PRODUCT_NAME "MasterSpace17dTests")
    target_sources(MasterSpace17dTests PRIVATE
        tests/space_plugin_tests.cpp
        Source/MasterSpace/PluginProcessor.cpp
        Source/MasterSpace/PluginEditor.cpp
        Source/MasterSpace/PresetManager.cpp
        Source/MasterSpace/PresetPanel.cpp
        Source/MasterSpace/StudioLook.cpp
        Source/MasterSpace/StudioEditor.cpp)
    target_compile_definitions(MasterSpace17dTests PRIVATE
        JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1
        MAS_ATMOS=1 MAS_RI=1 MAS_EQ=1 MAS_STUDIO=1 MAS_STUDIO_DYN=1
        JucePlugin_Name="MASTER SPACE 1.7d DYNAMIC TEST" JucePlugin_VersionString="1.7.0")
    target_link_libraries(MasterSpace17dTests PRIVATE
        MasterSpaceStudioPresets MasStudioFonts MasBrandData juce::juce_audio_utils juce::juce_dsp
        juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
endif()
