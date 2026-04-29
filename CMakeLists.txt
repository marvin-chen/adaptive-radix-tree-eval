cmake_minimum_required(VERSION 3.20)

project(adaptive_radix_tree_eval
    VERSION 0.1.0
    LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

option(ART_BUILD_UPSTREAM_TESTS "Build the original libart Check-based tests" OFF)

add_library(art STATIC
    src/art.c
    src/art_nodes.c
    src/node4.c
    src/node16.c
    src/node48.c
    src/node256.c)

target_include_directories(art
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_definitions(art
    PRIVATE
        _GNU_SOURCE)

if(MSVC)
    target_compile_options(art PRIVATE /W4)
else()
    target_compile_options(art PRIVATE -Wall -Wextra -Wpedantic)
endif()

enable_testing()

add_executable(test_correctness
    tests/test_correctness.c)

target_link_libraries(test_correctness
    PRIVATE
        art)

target_compile_definitions(test_correctness
    PRIVATE
        ART_PROJECT_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\")

add_test(NAME test_correctness COMMAND test_correctness)

add_executable(bench_original
    bench/bench_original.c)

target_link_libraries(bench_original
    PRIVATE
        art)

target_compile_definitions(bench_original
    PRIVATE
        ART_PROJECT_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\")

if(ART_BUILD_UPSTREAM_TESTS)
    find_path(CHECK_INCLUDE_DIR check.h)
    find_library(CHECK_LIBRARY check)

    if(NOT CHECK_INCLUDE_DIR OR NOT CHECK_LIBRARY)
        message(FATAL_ERROR
            "ART_BUILD_UPSTREAM_TESTS=ON requires the Check unit-test library. "
            "Install Check or configure with -DART_BUILD_UPSTREAM_TESTS=OFF.")
    endif()

    add_executable(test_art_upstream
        tests/runner.c)

    target_include_directories(test_art_upstream
        PRIVATE
            ${CHECK_INCLUDE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/src)

    target_link_libraries(test_art_upstream
        PRIVATE
            art
            ${CHECK_LIBRARY})

    add_test(NAME test_art_upstream COMMAND test_art_upstream)
    set_tests_properties(test_art_upstream
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()
