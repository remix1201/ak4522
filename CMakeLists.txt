cmake_minimum_required(VERSION 3.24)

project(ak4522
    VERSION 0.0.1
    DESCRIPTION "AKM AK4522VF DAC emulation library"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(AK4522_BUILD_TESTS "Build the test suite" ${PROJECT_IS_TOP_LEVEL})
option(AK4522_SANITIZE "Build with address and undefined-behavior sanitizers" OFF)
option(AK4522_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

# Shared compile settings for every project target.
add_library(ak4522_options INTERFACE)
if(MSVC)
    target_compile_options(ak4522_options INTERFACE /W4 /permissive-)
    if(AK4522_WARNINGS_AS_ERRORS)
        target_compile_options(ak4522_options INTERFACE /WX)
    endif()
else()
    target_compile_options(ak4522_options INTERFACE
        -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion -Wshadow
        -Wdouble-promotion -Wold-style-cast)
    if(AK4522_WARNINGS_AS_ERRORS)
        target_compile_options(ak4522_options INTERFACE -Werror)
    endif()
    if(AK4522_SANITIZE)
        target_compile_options(ak4522_options INTERFACE
            -fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
        target_link_options(ak4522_options INTERFACE -fsanitize=address,undefined)
    endif()
endif()

# Framework-independent core library.
add_library(ak4522
    src/core/config_validation.cpp
    src/core/control_timeline.cpp
    src/core/dac_core.cpp
    src/core/pcm.cpp
    src/core/soft_mute.cpp
    src/core/status.cpp
    src/core/version.cpp
    src/dsp/deemphasis.cpp
    src/dsp/interpolator.cpp
    src/dsp/reconstruction.cpp
    src/dsp/resampler.cpp
    src/serial/serial_decoder.cpp
    src/serial/timing_validator.cpp)
add_library(ak4522::ak4522 ALIAS ak4522)
target_include_directories(ak4522
    PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
    PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(ak4522 PRIVATE ak4522_options)
target_compile_definitions(ak4522 PRIVATE
    AK4522_VERSION_STRING="${PROJECT_VERSION}")

if(AK4522_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)

    # Documentation consistency checks (standard-library Python only).
    find_package(Python3 3.9 COMPONENTS Interpreter)
    if(Python3_Interpreter_FOUND)
        add_test(NAME docs.consistency
            COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/tools/check_docs.py
                --root ${PROJECT_SOURCE_DIR})
        add_test(NAME docs.checker_self_test
            COMMAND Python3::Interpreter ${PROJECT_SOURCE_DIR}/tools/test_check_docs.py)
        set_tests_properties(docs.consistency docs.checker_self_test
            PROPERTIES LABELS "docs;unit")
    else()
        message(WARNING "Python 3.9+ not found; documentation checks are disabled")
    endif()

    # Response analysis tools (numpy/scipy through the uv-locked environment).
    find_program(AK4522_UV uv)
    if(AK4522_UV)
        add_test(NAME tools.analysis
            COMMAND ${AK4522_UV} run --directory ${PROJECT_SOURCE_DIR}/tools
                python -m unittest discover -s tests -t .)
        set_tests_properties(tools.analysis PROPERTIES LABELS "tools" TIMEOUT 600)

        # Measure the C++ implementation's impulse response against the gates.
        add_executable(ak4522_impulse_dump tools/cpp/impulse_dump.cpp)
        target_include_directories(ak4522_impulse_dump PRIVATE ${PROJECT_SOURCE_DIR}/src)
        target_link_libraries(ak4522_impulse_dump PRIVATE ak4522 ak4522_options)

        set(interpolator_ir ${CMAKE_BINARY_DIR}/dsp/interpolator_impulse.txt)
        file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/dsp)
        add_test(NAME dsp.interpolator_dump
            COMMAND ak4522_impulse_dump interpolator ${interpolator_ir} 64)
        set_tests_properties(dsp.interpolator_dump PROPERTIES
            LABELS "dsp" FIXTURES_SETUP interpolator_ir)
        foreach(rate 32000 44100 48000)
            math(EXPR high "8 * ${rate}")
            add_test(NAME dsp.interpolator_response_${rate}
                COMMAND ${AK4522_UV} run --directory ${PROJECT_SOURCE_DIR}/tools
                    python analyze_response.py ${interpolator_ir}
                    --fs-high ${high} --dac-rate ${rate} --no-delay)
            set_tests_properties(dsp.interpolator_response_${rate} PROPERTIES
                LABELS "dsp" FIXTURES_REQUIRED interpolator_ir TIMEOUT 300)
        endforeach()

        set(core_linear_ir ${CMAKE_BINARY_DIR}/dsp/core_linear_impulse.txt)
        add_test(NAME dsp.core_linear_dump
            COMMAND ak4522_impulse_dump core-linear ${core_linear_ir} 1000)
        set_tests_properties(dsp.core_linear_dump PROPERTIES
            LABELS "dsp" FIXTURES_SETUP core_linear_ir)
        foreach(rate 32000 44100 48000)
            math(EXPR high "8 * ${rate}")
            add_test(NAME dsp.core_linear_response_${rate}
                COMMAND ${AK4522_UV} run --directory ${PROJECT_SOURCE_DIR}/tools
                    python analyze_response.py ${core_linear_ir}
                    --fs-high ${high} --dac-rate ${rate} --core)
            set_tests_properties(dsp.core_linear_response_${rate} PROPERTIES
                LABELS "dsp" FIXTURES_REQUIRED core_linear_ir TIMEOUT 300)
        endforeach()

        # Every M2 response gate on the public DacCore output: digital
        # response, stopband images, delay, and combined response.
        set(dac_core_ir ${CMAKE_BINARY_DIR}/dsp/dac_core_impulse.txt)
        add_test(NAME dsp.dac_core_dump
            COMMAND ak4522_impulse_dump dac-core ${dac_core_ir} 1000)
        set_tests_properties(dsp.dac_core_dump PROPERTIES
            LABELS "dsp" FIXTURES_SETUP dac_core_ir)
        foreach(rate 32000 44100 48000)
            math(EXPR high "8 * ${rate}")
            add_test(NAME dsp.dac_core_response_${rate}
                COMMAND ${AK4522_UV} run --directory ${PROJECT_SOURCE_DIR}/tools
                    python analyze_response.py ${dac_core_ir}
                    --fs-high ${high} --dac-rate ${rate} --combined)
            set_tests_properties(dsp.dac_core_response_${rate} PROPERTIES
                LABELS "dsp" FIXTURES_REQUIRED dac_core_ir TIMEOUT 300)
        endforeach()
    else()
        message(WARNING "uv not found; analysis tool tests are disabled")
    endif()
endif()
