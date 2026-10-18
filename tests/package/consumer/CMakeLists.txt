# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 Ross Baker

# A project outside the library's build that uses it as any user would
# (REQ-API-03): through find_package, or through add_subdirectory when
# AK4522_SOURCE_DIR is given.
cmake_minimum_required(VERSION 3.24)
project(ak4522_consumer LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(AK4522_SOURCE_DIR)
    add_subdirectory(${AK4522_SOURCE_DIR} ak4522)
else()
    find_package(ak4522 0.0 REQUIRED CONFIG)
endif()

add_executable(consumer_cpp main.cpp)
target_link_libraries(consumer_cpp PRIVATE ak4522::ak4522)

# A C program; the C++ library's link language selects the C++ linker.
add_executable(consumer_c main.c)
set_target_properties(consumer_c PROPERTIES C_STANDARD 99)
target_link_libraries(consumer_c PRIVATE ak4522::ak4522)
if(UNIX AND NOT APPLE)
    target_link_libraries(consumer_c PRIVATE m)
endif()

# Windows finds DLLs next to the executable.
if(WIN32 AND CONSUMER_COPY_DLLS)
    foreach(target consumer_cpp consumer_c)
        add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different $<TARGET_RUNTIME_DLLS:${target}> $<TARGET_FILE_DIR:${target}>
            COMMAND_EXPAND_LISTS)
    endforeach()
endif()

enable_testing()
add_test(NAME consumer_cpp COMMAND consumer_cpp)
add_test(NAME consumer_c COMMAND consumer_c)
set_tests_properties(consumer_cpp consumer_c PROPERTIES PASS_REGULAR_EXPRESSION "consumer ok")
