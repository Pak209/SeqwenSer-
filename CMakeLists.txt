cmake_minimum_required(VERSION 3.22)

project(Mangle VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version" FORCE)
endif()

option(MANGLE_BUILD_PLUGIN "Build the JUCE plugin (fetches JUCE)" ON)
option(MANGLE_BUILD_TESTS  "Build unit tests" ON)
option(MANGLE_VST3         "Also build the VST3 format" OFF)
option(MANGLE_STANDALONE   "Also build the Standalone app" ON)
option(MANGLE_COPY_AFTER_BUILD "Copy the plugin into the user plug-in folder after building" OFF)
option(MANGLE_DISTRIBUTION "Release build for Developer ID signing (see docs/RELEASING.md)" OFF)

# Same signing policy as Pitch Lane: a distribution build must not carry the JUCE ad-hoc signature.
if(MANGLE_DISTRIBUTION AND MANGLE_COPY_AFTER_BUILD)
    message(FATAL_ERROR "MANGLE_DISTRIBUTION=ON requires MANGLE_COPY_AFTER_BUILD=OFF.")
endif()
if(MANGLE_DISTRIBUTION AND CMAKE_GENERATOR STREQUAL "Xcode")
    message(FATAL_ERROR "MANGLE_DISTRIBUTION=ON is meant for the Ninja/Makefiles build used by release.yml.")
endif()

if(MANGLE_BUILD_TESTS)
    enable_testing()
endif()

# Portable DSP core (no JUCE)
add_subdirectory(core)

if(MANGLE_BUILD_PLUGIN)
    include(FetchContent)
    # JUCE 8, pinned. For offline builds: -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        8.0.15
        GIT_SHALLOW    TRUE
        GIT_PROGRESS   TRUE)
    FetchContent_MakeAvailable(JUCE)
    add_subdirectory(plugin)
endif()
