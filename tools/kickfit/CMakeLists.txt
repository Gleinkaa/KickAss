cmake_minimum_required(VERSION 3.22)
project(KickFit VERSION 0.1.0)
set(CMAKE_CXX_STANDARD 17)
get_filename_component(KA "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(JUCE_DIR "$ENV{HOME}/apps/JUCE" CACHE PATH "JUCE checkout")
add_subdirectory(${JUCE_DIR} JUCE)
juce_add_console_app(KickFit PRODUCT_NAME "KickFit")
target_sources(KickFit PRIVATE main.cpp ${KA}/Source/PluginProcessor.cpp ${KA}/Source/PresetManager.cpp
               ${KA}/Source/KickEngine.cpp ${KA}/Source/CrashLog.cpp)
target_include_directories(KickFit PRIVATE ${KA}/Source)
target_compile_definitions(KickFit PRIVATE JucePlugin_Name="KickAss" KICKASS_HEADLESS_TESTS=1
    JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_DISPLAY_SPLASH_SCREEN=0)
target_link_libraries(KickFit PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
    juce::juce_recommended_config_flags)
