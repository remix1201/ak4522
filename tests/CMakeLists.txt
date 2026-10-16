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
    smoke_test.cpp)
target_link_libraries(ak4522_tests PRIVATE
    ak4522::ak4522 ak4522_options Catch2::Catch2WithMain)

file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/test-results)

# Catch2 tags become CTest labels: [unit] fast checks, [dsp] response gates,
# [soak] long runs. Run a subset with `ctest -L unit`.
catch_discover_tests(ak4522_tests
    ADD_TAGS_AS_LABELS
    REPORTER junit
    OUTPUT_DIR ${CMAKE_BINARY_DIR}/test-results
    OUTPUT_SUFFIX .xml)
