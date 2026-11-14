cmake_minimum_required(VERSION 3.22)
project(PolySwell2 VERSION 2.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(PolySwell
    COMPANY_NAME "YourName"
    PLUGIN_MANUFACTURER_CODE Ynam
    PLUGIN_CODE Psw2
    FORMATS VST3
    PRODUCT_NAME "PolySwell II"
    VST3_CATEGORIES Fx
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT TRUE   # MIDI out of the tracked notes (Reaper / Cantabile route it; see CLAUDE.md for Cubase)
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

# violin body IRs (owner-supplied), embedded: BinaryData::violin_*_wav
juce_add_binary_data(PolySwellData SOURCES
    resources/ir/violin_a.wav resources/ir/violin_en.wav resources/ir/violin_eu.wav resources/ir/violin_g1.wav
    resources/ir/violin_g2.wav resources/ir/violin_s1.wav resources/ir/violin_s2.wav)

target_sources(PolySwell PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp)

target_compile_definitions(PolySwell PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_include_directories(PolySwell PRIVATE Source)

target_link_libraries(PolySwell
    PRIVATE juce::juce_audio_utils juce::juce_dsp PolySwellData
    PUBLIC  juce::juce_recommended_config_flags
            juce::juce_recommended_lto_flags
            juce::juce_recommended_warning_flags)

# Standalone DSP tests (no JUCE needed): cmake --build . --target engine_test
add_executable(engine_test tests/engine_test.cpp)
target_include_directories(engine_test PRIVATE Source)
# the tests keep many engines (each a few hundred KB with the pitch tracker) on the stack
if (MSVC)
    target_link_options(engine_test PRIVATE /STACK:16777216)
endif()

# Dev tool: render the editor to PNGs without a DAW (not part of the plugin)
juce_add_console_app(ui_snapshot PRODUCT_NAME "ui_snapshot")
target_sources(ui_snapshot PRIVATE tools/ui_snapshot.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp)
target_include_directories(ui_snapshot PRIVATE Source)
target_compile_definitions(ui_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=1)
target_link_libraries(ui_snapshot PRIVATE juce::juce_audio_utils juce::juce_dsp PolySwellData juce::juce_recommended_config_flags)

# Dev tool: CPU cost per DSP feature (no JUCE)
add_executable(engine_bench tools/engine_bench.cpp)
target_include_directories(engine_bench PRIVATE Source)
