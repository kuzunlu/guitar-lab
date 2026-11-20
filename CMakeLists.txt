cmake_minimum_required(VERSION 3.25)

project(GuitarLab VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# The version comes from the newest git tag (like v0.1.0). A release build shows the tag,
# other builds show how far past it they are, like v0.1.0-2-gabc1234.
set(GUITAR_LAB_VERSION_TEXT "dev")
set(GUITAR_LAB_VERSION_NUMBER "0.0.0")
find_package(Git QUIET)

if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" describe --tags --match "v[0-9]*"
                    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                    OUTPUT_VARIABLE gitDescribe
                    OUTPUT_STRIP_TRAILING_WHITESPACE
                    ERROR_QUIET
                    RESULT_VARIABLE gitDescribeResult)

    if(gitDescribeResult EQUAL 0 AND gitDescribe MATCHES "^v([0-9]+[.][0-9]+[.][0-9]+)")
        set(GUITAR_LAB_VERSION_TEXT "${gitDescribe}")
        set(GUITAR_LAB_VERSION_NUMBER "${CMAKE_MATCH_1}")
    endif()
endif()

message(STATUS "Guitar Lab version: ${GUITAR_LAB_VERSION_TEXT}")

# Put the Visual C++ runtime inside the plugin, so it works on PCs without the Visual C++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

include(FetchContent)

# JUCE is pinned to an exact release tag.
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        9.0.5
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# ---------------------------------------------------------------------------
# Music logic library (plain C++, no JUCE) so it can be unit tested alone.
# ---------------------------------------------------------------------------
add_library(GuitarLabCore STATIC
    Source/Theory/Note.cpp
    Source/Theory/Instrument.cpp
    Source/Theory/Chord.cpp
    Source/Theory/Reharm.cpp
    Source/Theory/Voicing.cpp
    Source/Theory/Trainer.cpp
    Source/Theory/ChordFinder.cpp
    Source/Midi/Phrase.cpp
    Source/Midi/MidiRenderer.cpp
    Source/Midi/PhraseRecorder.cpp
    Source/Midi/TabExport.cpp
    Source/Midi/ChordTimeline.cpp
    Source/Tracking/PitchDetector.cpp
    Source/Tracking/Spectrum.cpp
    Source/Tracking/Calibration.cpp
    Source/Tracking/GuitarTracker.cpp
    Source/Tracking/PolyTracker.cpp
)
target_include_directories(GuitarLabCore PUBLIC Source)

# ---------------------------------------------------------------------------
# The plugin: VST3 + Standalone app.
# ---------------------------------------------------------------------------
juce_add_plugin(GuitarLab
    VERSION                    ${GUITAR_LAB_VERSION_NUMBER}
    COMPANY_NAME               "Kursat"
    PLUGIN_MANUFACTURER_CODE   Kuzu
    PLUGIN_CODE                GtLb
    PRODUCT_NAME               "Guitar Lab"
    FORMATS                    VST3 Standalone
    IS_SYNTH                   FALSE
    NEEDS_MIDI_INPUT           TRUE
    NEEDS_MIDI_OUTPUT          TRUE
    IS_MIDI_EFFECT             FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD    FALSE
    VST3_CATEGORIES            Fx
)

target_sources(GuitarLab PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/Audio/PluckSynth.cpp
    Source/UI/FretboardComponent.cpp
    Source/UI/PianoComponent.cpp
    Source/UI/InstrumentBar.cpp
    Source/UI/ReharmPanel.cpp
    Source/UI/TrainerPanel.cpp
    Source/UI/PhrasePanel.cpp
    Source/UI/TrackerPanel.cpp
    Source/UI/ShapesPanel.cpp
    Source/UI/IntonationView.cpp
    Source/UI/LighthouseTuner.cpp
)

target_compile_definitions(GuitarLab PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    "GUITAR_LAB_VERSION=\"${GUITAR_LAB_VERSION_TEXT}\""
)

target_link_libraries(GuitarLab
    PRIVATE
        GuitarLabCore
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# ---------------------------------------------------------------------------
# Tests for the music logic (no JUCE needed).
# ---------------------------------------------------------------------------
enable_testing()
add_executable(GuitarLabTests Tests/CoreTests.cpp)
# Always build/GuitarLabTests.exe ($<1:...> stops multi-config generators from adding a Release folder).
set_target_properties(GuitarLabTests PROPERTIES RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}>")
target_link_libraries(GuitarLabTests PRIVATE GuitarLabCore)
add_test(NAME CoreTests COMMAND GuitarLabTests)

# ---------------------------------------------------------------------------
# Pictures for the user manual (docs/manual/img). Off by default: tools\make-manual-images.bat turns it on.
# ---------------------------------------------------------------------------
option(GUITAR_LAB_SCREENSHOTS "Build the tool that makes the manual's pictures" OFF)
if(GUITAR_LAB_SCREENSHOTS)
    juce_add_console_app(GuitarLabScreenshots PRODUCT_NAME "GuitarLabScreenshots")
    get_target_property(GUITAR_LAB_UI_SOURCES GuitarLab SOURCES)
    target_sources(GuitarLabScreenshots PRIVATE tools/screenshots/main.cpp ${GUITAR_LAB_UI_SOURCES})
    target_include_directories(GuitarLabScreenshots PRIVATE Source)
    target_compile_definitions(GuitarLabScreenshots PRIVATE
        JUCE_WEB_BROWSER=0
        JUCE_USE_CURL=0
        JUCE_MODAL_LOOPS_PERMITTED=1
        "GUITAR_LAB_VERSION=\"${GUITAR_LAB_VERSION_TEXT}\""
    )
    target_link_libraries(GuitarLabScreenshots PRIVATE
        GuitarLabCore
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_gui_extra
        juce::juce_recommended_config_flags
    )
endif()
