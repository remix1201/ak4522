include(FetchContent)

# Pinned to an exact release commit (v3.16.0) for reproducible builds.
FetchContent_Declare(Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG 317ac1ed4c0bb6e6b91eafc817e05c488feffcb3 # v3.16.0
    GIT_SHALLOW FALSE
    SYSTEM)
FetchContent_MakeAvailable(Catch2)

list(APPEND CMAKE_MODULE_PATH ${catch2_SOURCE_DIR}/extras)
include(Catch)

add_executable(ak4522_tests
    allocation_counter_test.cpp
    analog_noise_test.cpp
    api_contract_test.cpp
    c_api_equivalence_test.cpp
    config_validation_test.cpp
    control_timeline_test.cpp
    dac_core_test.cpp
    dac_stream_test.cpp
    deemphasis_test.cpp
    extended_rate_test.cpp
    golden_test.cpp
    host_processor_test.cpp
    interpolator_test.cpp
    output_stage_test.cpp
    pcm_test.cpp
    reconstruction_test.cpp
    resampler_test.cpp
    serial_decoder_test.cpp
    smoke_test.cpp
    soft_mute_test.cpp
    status_test.cpp
    timing_validator_test.cpp
    wide_math_test.cpp
    support/allocation_counter.cpp)
# Tests may include internal headers under src/.
target_include_directories(ak4522_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_definitions(ak4522_tests PRIVATE
    AK4522_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data")
find_package(Threads REQUIRED)
target_link_libraries(ak4522_tests PRIVATE
    ak4522::ak4522 ak4522_options Catch2::Catch2WithMain Threads::Threads)

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/test-results)

# Catch2 tags become CTest labels: [unit] fast checks, [dsp] response gates,
# [soak] long runs. Run a subset with `ctest -L unit`.
# A per-test timeout turns a hang (for example in a mutation check) into a
# failure within minutes instead of CTest's 25-minute default.
catch_discover_tests(ak4522_tests
    ADD_TAGS_AS_LABELS
    PROPERTIES TIMEOUT 120
    REPORTER junit
    OUTPUT_DIR ${CMAKE_BINARY_DIR}/test-results
    OUTPUT_SUFFIX .xml)

# Host-rate matrix verification: slower, with its own timeout and label.
add_executable(ak4522_matrix_tests
    matrix/host_matrix_test.cpp
    support/allocation_counter.cpp)
target_include_directories(ak4522_matrix_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(ak4522_matrix_tests PRIVATE ak4522::ak4522 ak4522_options Catch2::Catch2WithMain)
catch_discover_tests(ak4522_matrix_tests
    ADD_TAGS_AS_LABELS
    PROPERTIES TIMEOUT 1800
    REPORTER junit
    OUTPUT_DIR ${CMAKE_BINARY_DIR}/test-results
    OUTPUT_SUFFIX .xml)

# The C API, built as C (REQ-API-02). Linked by the C++ linker because the
# library is C++.
enable_language(C)
add_executable(ak4522_c_api_test c_api/c_api_test.c)
set_target_properties(ak4522_c_api_test PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF LINKER_LANGUAGE CXX)
target_link_libraries(ak4522_c_api_test PRIVATE ak4522::ak4522)
if(MSVC)
    target_compile_options(ak4522_c_api_test PRIVATE /W4)
else()
    target_compile_options(ak4522_c_api_test PRIVATE -Wall -Wextra -Wpedantic -Wconversion)
    if(AK4522_SANITIZE)
        target_compile_options(ak4522_c_api_test PRIVATE -fsanitize=address,undefined -fno-omit-frame-pointer)
        target_link_options(ak4522_c_api_test PRIVATE -fsanitize=address,undefined)
    endif()
endif()
if(AK4522_WARNINGS_AS_ERRORS)
    target_compile_options(ak4522_c_api_test PRIVATE $<IF:$<C_COMPILER_ID:MSVC>,/WX,-Werror>)
endif()
if(UNIX AND NOT APPLE)
    target_link_libraries(ak4522_c_api_test PRIVATE m)
endif()
add_test(NAME capi.c_test COMMAND ak4522_c_api_test)
set_tests_properties(capi.c_test PROPERTIES LABELS "unit;capi")
