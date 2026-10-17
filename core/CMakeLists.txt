add_library(mangle_core STATIC
    src/Dsp.cpp
    src/Engine.cpp
    src/Sampler.cpp
    src/capi.cpp)

target_include_directories(mangle_core PUBLIC include capi)
target_compile_features(mangle_core PUBLIC cxx_std_17)
set_target_properties(mangle_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
    target_compile_options(mangle_core PRIVATE /W4)
else()
    target_compile_options(mangle_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

if(MANGLE_BUILD_TESTS)
    add_executable(mangle_core_tests
        tests/TestMain.cpp
        tests/TestSignals.h
        tests/DspTests.cpp
        tests/EngineTests.cpp
        tests/TempoTests.cpp
        tests/SamplerTests.cpp)
    target_link_libraries(mangle_core_tests PRIVATE mangle_core)
    add_test(NAME mangle_core_tests COMMAND mangle_core_tests)
endif()
