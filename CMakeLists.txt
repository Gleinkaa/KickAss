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

juce_add_plugin(KickAss
    COMPANY_NAME                "Gleinkaa"
    PLUGIN_MANUFACTURER_CODE    Glka
    PLUGIN_CODE                 Kick
    PRODUCT_NAME                "KickAss"
    FORMATS                     VST3 Standalone
    IS_SYNTH                    TRUE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE      # use scripts/deploy_vst3.bat (needs elevated shell)
)

target_sources(KickAss PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/KickEngine.cpp
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
