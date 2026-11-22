cmake_minimum_required(VERSION 3.22)
project(PolySwell2 VERSION 2.0.1)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.6
    GIT_SHALLOW ON)
FetchContent_MakeAvailable(JUCE)

# VST3 everywhere, Audio Unit too on a Mac (Logic, GarageBand). Universal binary on a Mac:
#   cmake -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
set(POLYSWELL_FORMATS VST3)
if (APPLE)
    list(APPEND POLYSWELL_FORMATS AU)
endif()

juce_add_plugin(PolySwell
    COMPANY_NAME "Benno"
    BUNDLE_ID com.benno.polyswell2
    PLUGIN_MANUFACTURER_CODE Ynam   # keep: with PLUGIN_CODE it is the plugin's identity in saved DAW sessions
    PLUGIN_CODE Psw2
    FORMATS ${POLYSWELL_FORMATS}
    PRODUCT_NAME "PolySwell II"
    VST3_CATEGORIES Fx
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE    # learned MIDI CCs (footswitch -> HOLD / ON, expression pedal -> a knob)
    NEEDS_MIDI_OUTPUT TRUE   # MIDI out of the tracked notes (Reaper / Cantabile route it; see CLAUDE.md for Cubase)
    IS_MIDI_EFFECT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

# violin body IRs (owner-supplied), embedded: BinaryData::violin_*_wav
juce_add_binary_data(PolySwellData SOURCES
    resources/ir/violin_a.wav resources/ir/violin_en.wav resources/ir/violin_eu.wav resources/ir/violin_g1.wav
    resources/ir/violin_g2.wav resources/ir/violin_s1.wav resources/ir/violin_s2.wav
    resources/skin/plate_sunburst.png resources/skin/plate_lagoon.png resources/skin/plate_midnight.png
    resources/skin/logo_sunburst.png resources/skin/logo_lagoon.png resources/skin/logo_midnight.png
    resources/skin/plate_night.png resources/skin/logo_night.png
    resources/skin/footswitch.png)

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
elseif (APPLE)
    target_link_options(engine_test PRIVATE -Wl,-stack_size,0x1000000)
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
