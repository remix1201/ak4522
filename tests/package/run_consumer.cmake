# Builds and runs tests/package/consumer against the library (REQ-API-03).
#
#   cmake -DMODE=install|subdirectory|shared -DSOURCE_DIR=... -DBUILD_DIR=...
#         -DWORK_DIR=... -DCONFIG=... -DGENERATOR=... [-DPLATFORM=...] -P run_consumer.cmake
#
# install:      installs the existing BUILD_DIR to a prefix and uses find_package
# subdirectory: the consumer adds the source tree with add_subdirectory
# shared:       builds and installs a shared library, then uses find_package
# pkgconfig:    installs the existing BUILD_DIR and compiles the C consumer with
#               the flags from pkg-config, without CMake (needs PKG_CONFIG and C_COMPILER)

foreach(var MODE SOURCE_DIR WORK_DIR CONFIG GENERATOR)
    if(NOT DEFINED ${var})
        message(FATAL_ERROR "${var} is required")
    endif()
endforeach()

set(generator_args -G "${GENERATOR}")
if(PLATFORM)
    list(APPEND generator_args -A "${PLATFORM}")
endif()

function(run)
    execute_process(COMMAND ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE out)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "failed (${result}): ${ARGN}\n${out}")
    endif()
endfunction()

file(REMOVE_RECURSE ${WORK_DIR})
set(prefix ${WORK_DIR}/prefix)
set(consumer_args)

if(MODE STREQUAL "install")
    run(${CMAKE_COMMAND} --install ${BUILD_DIR} --config ${CONFIG} --prefix ${prefix})
    list(APPEND consumer_args -DCMAKE_PREFIX_PATH=${prefix})
elseif(MODE STREQUAL "shared")
    run(${CMAKE_COMMAND} -S ${SOURCE_DIR} -B ${WORK_DIR}/library ${generator_args}
        -DCMAKE_BUILD_TYPE=${CONFIG} -DBUILD_SHARED_LIBS=ON -DAK4522_INSTALL=ON
        -DAK4522_BUILD_TESTS=OFF -DAK4522_BUILD_EXAMPLES=OFF)
    run(${CMAKE_COMMAND} --build ${WORK_DIR}/library --config ${CONFIG} --parallel)
    run(${CMAKE_COMMAND} --install ${WORK_DIR}/library --config ${CONFIG} --prefix ${prefix})
    list(APPEND consumer_args -DCMAKE_PREFIX_PATH=${prefix} -DCONSUMER_COPY_DLLS=ON)
elseif(MODE STREQUAL "pkgconfig")
    run(${CMAKE_COMMAND} --install ${BUILD_DIR} --config ${CONFIG} --prefix ${prefix})
    file(GLOB_RECURSE pc_files ${prefix}/*/ak4522.pc)
    list(GET pc_files 0 pc_file)
    get_filename_component(pc_dir ${pc_file} DIRECTORY)
    set(ENV{PKG_CONFIG_PATH} ${pc_dir})
    execute_process(COMMAND ${PKG_CONFIG} --cflags --libs --static ak4522
        RESULT_VARIABLE result OUTPUT_VARIABLE flags OUTPUT_STRIP_TRAILING_WHITESPACE)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "pkg-config failed")
    endif()
    separate_arguments(flags UNIX_COMMAND "${flags}")
    file(MAKE_DIRECTORY ${WORK_DIR}/consumer)
    run(${C_COMPILER} -std=c99 ${SOURCE_DIR}/tests/package/consumer/main.c ${flags} -lm -o ${WORK_DIR}/consumer/consumer_c)
    execute_process(COMMAND ${WORK_DIR}/consumer/consumer_c RESULT_VARIABLE result OUTPUT_VARIABLE out)
    if(NOT result EQUAL 0 OR NOT out MATCHES "consumer ok")
        message(FATAL_ERROR "consumer failed (${result}): ${out}")
    endif()
    message(STATUS "consumer (pkgconfig) ok: ${flags}")
    return()
elseif(MODE STREQUAL "subdirectory")
    list(APPEND consumer_args -DAK4522_SOURCE_DIR=${SOURCE_DIR})
else()
    message(FATAL_ERROR "unknown MODE ${MODE}")
endif()

run(${CMAKE_COMMAND} -S ${SOURCE_DIR}/tests/package/consumer -B ${WORK_DIR}/consumer ${generator_args}
    -DCMAKE_BUILD_TYPE=${CONFIG} ${consumer_args})
run(${CMAKE_COMMAND} --build ${WORK_DIR}/consumer --config ${CONFIG} --parallel)
run(${CMAKE_CTEST_COMMAND} --test-dir ${WORK_DIR}/consumer -C ${CONFIG} --output-on-failure)
message(STATUS "consumer (${MODE}) ok")
