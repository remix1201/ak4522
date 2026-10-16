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
option(AK4522_BUILD_EXAMPLES "Build the example programs" ${PROJECT_IS_TOP_LEVEL})
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
    src/analog/analog_noise.cpp
    src/analog/output_stage.cpp
    src/core/config_validation.cpp
    src/core/control_timeline.cpp
    src/core/dac_core.cpp
    src/core/host_processor.cpp
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

if(AK4522_BUILD_EXAMPLES)
    add_executable(ak4522_host_processor_example examples/host_processor_example.cpp)
    target_link_libraries(ak4522_host_processor_example PRIVATE ak4522::ak4522 ak4522_options)
endif()

if(AK4522_BUILD_TESTS)
    enable_testing()
    if(AK4522_BUILD_EXAMPLES)
        add_test(NAME examples.host_processor COMMAND ak4522_host_processor_example)
        set_tests_properties(examples.host_processor PROPERTIES
            LABELS "unit;examples" PASS_REGULAR_EXPRESSION "after mute:  silent")
    endif()
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

        # NominalAnalog captures measured by tools/measure_profile.py
        # (AGENTS.md section 13.2 gates 4 to 6, at the datasheet's 44.1 kHz).
        add_executable(ak4522_capture tools/cpp/capture.cpp)
        target_link_libraries(ak4522_capture PRIVATE ak4522 ak4522_options)
        function(ak4522_profile_gate name metric expect tolerance)
            set(file ${CMAKE_BINARY_DIR}/dsp/${name}.f64)
            set(tone 1000)
            list(FIND ARGN --tone tone_index)
            if(tone_index GREATER -1)
                math(EXPR tone_index "${tone_index} + 1")
                list(GET ARGN ${tone_index} tone)
            endif()
            add_test(NAME analog.${name}_capture COMMAND ak4522_capture ${file} ${ARGN})
            set_tests_properties(analog.${name}_capture PROPERTIES
                LABELS "analog" FIXTURES_SETUP ${name})
            add_test(NAME analog.${name}
                COMMAND ${AK4522_UV} run --directory ${PROJECT_SOURCE_DIR}/tools
                    python measure_profile.py ${file} --rate 44100 --metric ${metric}
                    --tone ${tone} --expect ${expect} --tolerance ${tolerance})
            set_tests_properties(analog.${name} PROPERTIES
                LABELS "analog" FIXTURES_REQUIRED ${name} TIMEOUT 600)
        endfunction()
        # S/N, A-weighted: 100 +/- 1 dB (D-A03, P-90), 10 s of zero input.
        ak4522_profile_gate(noise_snr_a snr_a 100 1 --noise --seconds 10)
        # The noise stays when muted (P-54): same S/N with a tone held by SMUTE.
        ak4522_profile_gate(noise_muted_snr_a snr_a 100 1 --noise --mute --tone 1000 --level 0 --seconds 10)
        # CCIR-ARM: 97.33 dB for this flat noise, against 96 dB typical
        # (DISC-08). Locked to the disclosed value, not to the datasheet.
        ak4522_profile_gate(noise_snr_ccir_arm snr_ccir_arm 97.33 0.3 --noise --seconds 10)
        # S/(N+D) 90 +/- 1 dB at the declared 0 dBFS level (D-A01, P-90, P-96),
        # for the full nominal profile and for each distortion shape.
        ak4522_profile_gate(sinad_nominal sinad 90 1 --nominal --tone 997 --level 0 --seconds 5)
        ak4522_profile_gate(sinad_equal sinad 90 1 --noise --distortion equal --tone 997 --level 0 --seconds 5)
        ak4522_profile_gate(sinad_odd sinad 90 1 --noise --distortion odd --tone 997 --level 0 --seconds 5)
        ak4522_profile_gate(sinad_even sinad 90 1 --noise --distortion even --tone 997 --level 0 --seconds 5)
        # Dynamic range: -60 dBFS tone, A-weighted residual, 100 +/- 1 dB (D-A02, P-90).
        ak4522_profile_gate(dynamic_range dynamic_range 100 1 --nominal --tone 997 --level -60 --seconds 10)

        # Golden Specification-profile outputs for tests/data (regenerate only
        # on a deliberate change to the clean signal path).
        add_executable(ak4522_golden_dump tools/cpp/golden_dump.cpp)
        target_include_directories(ak4522_golden_dump PRIVATE ${PROJECT_SOURCE_DIR}/tests)
        target_link_libraries(ak4522_golden_dump PRIVATE ak4522 ak4522_options)

        # Wrapper response measurement for docs/responses.md.
        add_executable(ak4522_host_response tools/cpp/host_response.cpp)
        target_include_directories(ak4522_host_response PRIVATE ${PROJECT_SOURCE_DIR}/tests)
        target_link_libraries(ak4522_host_response PRIVATE ak4522 ak4522_options)

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
        foreach(rate 44100) # the datasheet's filter-table rate (D-F09)
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
        foreach(rate 44100) # the datasheet's filter-table rate (D-F09)
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
        foreach(rate 44100) # the datasheet's filter-table rate (D-F09)
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
