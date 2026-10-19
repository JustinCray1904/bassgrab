cmake_minimum_required(VERSION 3.22)
# Static MSVC runtime: the .vst3 then runs on any Windows PC without the VC++ redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "10.13" CACHE STRING "")
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "")
endif()

project(BassGrab VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# JUCE is downloaded automatically on first configure (needs git + internet once).
# If this tag is ever unavailable, any 8.x tag works.
include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.6
    GIT_SHALLOW    ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(BassGrab
    COMPANY_NAME                BassGrab
    PLUGIN_MANUFACTURER_CODE    Bgrb
    PLUGIN_CODE                 Bgr1
    PRODUCT_NAME                "BassGrab"
    FORMATS                     VST3 Standalone      # VST3 loads in FL Studio; Standalone is handy for testing
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            FALSE
    NEEDS_MIDI_OUTPUT           FALSE
    COPY_PLUGIN_AFTER_BUILD     FALSE                # copying into Program Files needs admin; do it by hand
    VST3_CATEGORIES             Fx Analyzer)

target_sources(BassGrab PRIVATE
    Source/BassAnalyzer.h
    Source/PluginProcessor.cpp
    Source/PluginProcessor.h
    Source/PluginEditor.cpp
    Source/PluginEditor.h)

target_compile_definitions(BassGrab PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1)

target_link_libraries(BassGrab
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)

# Optional: `cmake -B build -DBASSGRAB_TESTS=ON` builds the standalone engine test (no JUCE needed to run it)
option(BASSGRAB_TESTS "Build the analysis-engine test" OFF)
if(BASSGRAB_TESTS)
    add_executable(bassgrab_test tests/test_analyzer.cpp)
    add_executable(bassgrab_mix_test tests/test_mix.cpp)
endif()
