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
    Source/PresetManager.cpp
    Source/BreakpointEditor.cpp
    Source/CrashLog.cpp
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

#==============================================================================
# Headless tests (Phase 6b #7) — curve save/load round-trip.
# Build with: cmake --build build --config Release --target KickAss_Tests
# Run:        build/KickAss_Tests_artefacts/Release/KickAss_Tests.exe
#==============================================================================
option(KICKASS_BUILD_TESTS "Build headless test executables" ON)

if(KICKASS_BUILD_TESTS)
    juce_add_console_app(KickAss_Tests PRODUCT_NAME "KickAss_Tests")

    target_sources(KickAss_Tests PRIVATE
        Tests/CurveRoundtripTest.cpp
        Source/PluginProcessor.cpp
        Source/PresetManager.cpp
        Source/KickEngine.cpp
        Source/CrashLog.cpp
    )

    target_include_directories(KickAss_Tests PRIVATE Source)

    target_compile_definitions(KickAss_Tests PRIVATE
        # The processor uses JucePlugin_Name in getName(); console-app builds
        # don't get the plugin-glue macros, so define the bare minimum we need.
        JucePlugin_Name="KickAss"
        KICKASS_HEADLESS_TESTS=1
        # Match the plugin's JUCE config so module headers compile identically.
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
    )

    target_link_libraries(KickAss_Tests
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_formats
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    #--------------------------------------------------------------------------
    # DSP regression net — non-silence / state round-trip / param-fuzz / extremes.
    # Separate console app (its own main()). Build/run:
    #   cmake --build build --config Release --target KickAss_DspTests
    #   build/KickAss_DspTests_artefacts/Release/KickAss_DspTests.exe
    #--------------------------------------------------------------------------
    juce_add_console_app(KickAss_DspTests PRODUCT_NAME "KickAss_DspTests")

    target_sources(KickAss_DspTests PRIVATE
        Tests/DspSafetyTest.cpp
        Source/PluginProcessor.cpp
        Source/PresetManager.cpp
        Source/KickEngine.cpp
        Source/CrashLog.cpp
    )

    target_include_directories(KickAss_DspTests PRIVATE Source)

    target_compile_definitions(KickAss_DspTests PRIVATE
        JucePlugin_Name="KickAss"
        KICKASS_HEADLESS_TESTS=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
    )

    target_link_libraries(KickAss_DspTests
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_formats
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    #--------------------------------------------------------------------------
    # SpectrumUtil test — exercises the header-only FFT helper (Source/SpectrumUtil.h)
    # used by the visualizer's spectrum view. GUI-free, depends only on juce_dsp.
    #   cmake --build build --config Release --target KickAss_SpectrumTests
    #   build/KickAss_SpectrumTests_artefacts/Release/KickAss_SpectrumTests.exe
    #--------------------------------------------------------------------------
    juce_add_console_app(KickAss_SpectrumTests PRODUCT_NAME "KickAss_SpectrumTests")

    target_sources(KickAss_SpectrumTests PRIVATE
        Tests/SpectrumUtilTest.cpp
    )

    target_include_directories(KickAss_SpectrumTests PRIVATE Source)

    target_compile_definitions(KickAss_SpectrumTests PRIVATE
        JucePlugin_Name="KickAss"
        KICKASS_HEADLESS_TESTS=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
    )

    target_link_libraries(KickAss_SpectrumTests
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_formats
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    #--------------------------------------------------------------------------
    # CrashLog test — GUI-free crash-logging helper (Source/CrashLog.{h,cpp}):
    # log-dir path, report formatting, file-write round-trip. No processor.
    #   cmake --build build --config Release --target KickAss_CrashTests
    #   build/KickAss_CrashTests_artefacts/Release/KickAss_CrashTests.exe
    #--------------------------------------------------------------------------
    juce_add_console_app(KickAss_CrashTests PRODUCT_NAME "KickAss_CrashTests")

    target_sources(KickAss_CrashTests PRIVATE
        Tests/CrashLogTest.cpp
        Source/CrashLog.cpp
    )

    target_include_directories(KickAss_CrashTests PRIVATE Source)

    target_compile_definitions(KickAss_CrashTests PRIVATE
        JucePlugin_Name="KickAss"
        KICKASS_HEADLESS_TESTS=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
    )

    target_link_libraries(KickAss_CrashTests
        PRIVATE
            juce::juce_core
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    #--------------------------------------------------------------------------
    # RenderWav test — KickAssProcessor::renderToWavFile, the shared render-to-disk
    # path behind EXPORT WAV + the v1.1 drag-out affordance. Constructs the
    # processor headlessly and validates the written WAV.
    #   cmake --build build --config Release --target KickAss_RenderTests
    #   build/KickAss_RenderTests_artefacts/Release/KickAss_RenderTests.exe
    #--------------------------------------------------------------------------
    juce_add_console_app(KickAss_RenderTests PRODUCT_NAME "KickAss_RenderTests")

    target_sources(KickAss_RenderTests PRIVATE
        Tests/RenderWavTest.cpp
        Source/PluginProcessor.cpp
        Source/PresetManager.cpp
        Source/KickEngine.cpp
        Source/CrashLog.cpp
    )

    target_include_directories(KickAss_RenderTests PRIVATE Source)

    target_compile_definitions(KickAss_RenderTests PRIVATE
        JucePlugin_Name="KickAss"
        KICKASS_HEADLESS_TESTS=1
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_DISPLAY_SPLASH_SCREEN=0
    )

    target_link_libraries(KickAss_RenderTests
        PRIVATE
            juce::juce_audio_utils
            juce::juce_audio_formats
            juce::juce_dsp
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags
    )

    # Convenience: emit a "ctest"-runnable wrapper too.
    enable_testing()
    add_test(NAME curve_roundtrip
             COMMAND $<TARGET_FILE:KickAss_Tests>)
    add_test(NAME dsp_safety
             COMMAND $<TARGET_FILE:KickAss_DspTests>)
    add_test(NAME spectrum_util
             COMMAND $<TARGET_FILE:KickAss_SpectrumTests>)
    add_test(NAME crash_log
             COMMAND $<TARGET_FILE:KickAss_CrashTests>)
    add_test(NAME render_wav
             COMMAND $<TARGET_FILE:KickAss_RenderTests>)
endif()
