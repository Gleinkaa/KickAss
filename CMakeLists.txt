cmake_minimum_required(VERSION 3.22)
project(KickAss VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE lives one level up from the project root (sibling layout — same as SnareRhythmGen)
# i.e. C:\Users\glein\JUCE  and  D:\GoogleDrive\B_projects\KickAss
# Allow override with -DJUCE_DIR=...
if(NOT DEFINED JUCE_DIR)
    set(JUCE_DIR "C:/Users/glein/JUCE" CACHE PATH "Path to JUCE source root")
endif()

add_subdirectory(${JUCE_DIR} ${CMAKE_CURRENT_BINARY_DIR}/JUCE)

# Per-OS plugin format matrix (ARCHITECTURE §8b)
#   Windows: VST3 + Standalone           (current)
#   macOS:   VST3 + AU + Standalone       (v1.x)
#   Linux:   VST3 + Standalone            (incidental)
# CLAP is added later via clap-juce-extensions — keep behind a CMake option.
if(APPLE)
    set(KICKASS_FORMATS VST3 AU Standalone)
elseif(WIN32)
    set(KICKASS_FORMATS VST3 Standalone)
else()
    set(KICKASS_FORMATS VST3 Standalone)
endif()

# macOS universal binary for v1.x
if(APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS arch" FORCE)
endif()

# Standalone-with-MIDI-keyboard helpers (so users see a clickable piano in the
# Standalone target without having to configure Audio/MIDI Settings first)
set(JUCE_STANDALONE_USE_OWN_MIDIINPUT TRUE)

juce_add_plugin(KickAss
    COMPANY_NAME                "Gleinkaa"
    BUNDLE_ID                   "com.gleinkaa.KickAss"          # macOS / iOS
    PLUGIN_MANUFACTURER_CODE    Glka
    PLUGIN_CODE                 Kick
    PRODUCT_NAME                "KickAss"
    FORMATS                     ${KICKASS_FORMATS}
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE                            # use scripts/deploy_vst3.bat (Win) / built-in default install (macOS)
    # macOS-only category hints
    AU_MAIN_TYPE                kAudioUnitType_MusicDevice
    VST3_CATEGORIES             Instrument Synth Drum
)

target_sources(KickAss PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/KickEngine.cpp
    Source/KickAssLookAndFeel.cpp
    Source/WaveformDisplay.cpp
)

target_include_directories(KickAss PRIVATE Source)

target_compile_definitions(KickAss PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(KickAss
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags
)
