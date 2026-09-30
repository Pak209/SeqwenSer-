# ---------------------------------------------------------------------------
# Mangle plugin (JUCE 8). AU instrument is the priority format (Logic Pro); Standalone is for
# quick testing; VST3 is one option away (-DMANGLE_VST3=ON).
# ---------------------------------------------------------------------------
set(MANGLE_FORMATS "")
if(APPLE)
    list(APPEND MANGLE_FORMATS AU)
endif()
if(MANGLE_STANDALONE)
    list(APPEND MANGLE_FORMATS Standalone)
endif()
if(MANGLE_VST3)
    list(APPEND MANGLE_FORMATS VST3)
endif()
if(NOT MANGLE_FORMATS)
    set(MANGLE_FORMATS Standalone)
endif()
message(STATUS "Mangle plugin formats: ${MANGLE_FORMATS}")

juce_add_plugin(Mangle
    PRODUCT_NAME "Mangle"                  # -> Mangle.component
    PLUGIN_NAME "Mangle"                   # name shown in Logic
    COMPANY_NAME "Pak209"                  # manufacturer name shown in Logic
    BUNDLE_ID "com.dkimoto.mangle"
    PLUGIN_MANUFACTURER_CODE Pk20
    PLUGIN_CODE Mngl
    DESCRIPTION "Sample mangler: drop in audio and chop, pitch, grain, filter and repeat it in time with Logic"
    VERSION ${PROJECT_VERSION}
    FORMATS ${MANGLE_FORMATS}
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    AU_SANDBOX_SAFE FALSE
    VST3_CATEGORIES Instrument Sampler
    COPY_PLUGIN_AFTER_BUILD ${MANGLE_COPY_AFTER_BUILD})

# Embedded assets: Inter font (SIL Open Font License 1.1, see resources/fonts/OFL.txt and docs/LICENSES.md).
juce_add_binary_data(MangleAssets
    HEADER_NAME MangleBinaryData.h
    NAMESPACE MangleBinaryData
    SOURCES
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/Inter-Regular.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/Inter-Medium.ttf
        ${CMAKE_CURRENT_SOURCE_DIR}/resources/fonts/Inter-SemiBold.ttf)
set_target_properties(MangleAssets PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

# All plugin sources live in an INTERFACE library so the tests can compile the very same
# processor/editor code without the plugin wrappers.
add_library(MangleShared INTERFACE)
target_sources(MangleShared INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PluginProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PluginEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Params.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Presets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/SampleLoader.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Theme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Log.cpp)
target_include_directories(MangleShared INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# Build id shown in the diagnostics log (the release workflow passes the full version).
if(NOT MANGLE_BUILD_ID)
    execute_process(COMMAND git rev-parse --short=7 HEAD
                    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                    OUTPUT_VARIABLE _mg_git_sha OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    set(MANGLE_BUILD_ID "${_mg_git_sha}")
    if(NOT MANGLE_BUILD_ID)
        set(MANGLE_BUILD_ID "local")
    endif()
endif()
target_compile_definitions(MangleShared INTERFACE
    MANGLE_BUILD_ID="${MANGLE_BUILD_ID}"
    MANGLE_VERSION="${CMAKE_PROJECT_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_USE_MP3AUDIOFORMAT=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)
target_link_libraries(MangleShared INTERFACE
    mangle_core
    MangleAssets
    juce::juce_audio_utils
    juce::juce_audio_processors
    juce::juce_audio_formats
    juce::juce_gui_basics)

target_link_libraries(Mangle
    PRIVATE
        MangleShared
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

if(MANGLE_BUILD_TESTS)
    juce_add_console_app(MangleTests PRODUCT_NAME "MangleTests")
    target_sources(MangleTests PRIVATE tests/PluginTests.cpp)
    target_compile_definitions(MangleTests PRIVATE
        JucePlugin_Name="Mangle"
        MANGLE_TESTS=1
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(MangleTests PRIVATE
        MangleShared
        juce::juce_recommended_config_flags
        juce::juce_recommended_warning_flags)
    if(NOT MSVC)
        target_compile_options(MangleTests PRIVATE -Wno-float-equal -Wno-sign-conversion)
    endif()
    add_test(NAME MangleTests COMMAND MangleTests)

    # Dev tool: render the editor to a PNG (not part of the test suite).
    juce_add_console_app(MangleSnapshot PRODUCT_NAME "MangleSnapshot")
    target_sources(MangleSnapshot PRIVATE tests/Snapshot.cpp)
    target_compile_definitions(MangleSnapshot PRIVATE JucePlugin_Name="Mangle" JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(MangleSnapshot PRIVATE MangleShared juce::juce_recommended_config_flags)
    set_target_properties(MangleSnapshot PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
