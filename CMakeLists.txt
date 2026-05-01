cmake_minimum_required(VERSION 3.20)

project(adaptive_radix_tree_eval
    VERSION 0.1.0
    LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

option(ART_BUILD_UPSTREAM_TESTS "Build the original libart Check-based tests" OFF)

set(ART_SOURCES
    src/art.c
    src/art_nodes.c
    src/node4.c
    src/node16.c
    src/node48.c
    src/node256.c)

set(ART_NODE32_SOURCES
    ${ART_SOURCES}
    src/node32.c)

set(ART_NODE64_SOURCES
    ${ART_SOURCES}
    src/node64.c)

set(ART_NODE32_NODE64_SOURCES
    ${ART_SOURCES}
    src/node32.c
    src/node64.c)

set(ART_PAPER6_INDEXED_SOURCES
    ${ART_SOURCES}
    src/node2.c
    src/node5.c
    src/node32.c
    src/node64.c)

set(ART_COUNT5_SOURCES
    ${ART_SOURCES}
    src/node32.c
    src/node64.c)

set(ART_COUNT7_SOURCES
    ${ART_SOURCES}
    src/node2.c
    src/node5.c
    src/node32.c
    src/node64.c)

function(configure_art_target target_name)
    target_include_directories(${target_name}
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/src)

    target_compile_definitions(${target_name}
        PRIVATE
            _GNU_SOURCE)

    if(MSVC)
        target_compile_options(${target_name} PRIVATE /W4)
    else()
        target_compile_options(${target_name} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_library(art STATIC ${ART_SOURCES})
configure_art_target(art)
target_compile_definitions(art
    PRIVATE
        ART_MENU_ORIGINAL4)

add_library(art_node32 STATIC ${ART_NODE32_SOURCES})
configure_art_target(art_node32)
target_compile_definitions(art_node32
    PRIVATE
        ART_MENU_NODE32_ARRAY)

add_library(art_node64 STATIC ${ART_NODE64_SOURCES})
configure_art_target(art_node64)
target_compile_definitions(art_node64
    PRIVATE
        ART_MENU_NODE64_INDEXED)

add_library(art_node32_node64 STATIC ${ART_NODE32_NODE64_SOURCES})
configure_art_target(art_node32_node64)
target_compile_definitions(art_node32_node64
    PRIVATE
        ART_MENU_NODE32_NODE64_INDEXED)

add_library(art_paper6_indexed STATIC ${ART_PAPER6_INDEXED_SOURCES})
configure_art_target(art_paper6_indexed)
target_compile_definitions(art_paper6_indexed
    PRIVATE
        ART_MENU_PAPER6_INDEXED)

add_library(art_node256_only STATIC ${ART_SOURCES})
configure_art_target(art_node256_only)
target_compile_definitions(art_node256_only
    PRIVATE
        ART_MENU_NODE256_ONLY)

function(add_count_art_library target_name menu_definition)
    add_library(${target_name} STATIC ${ARGN})
    configure_art_target(${target_name})
    target_compile_definitions(${target_name}
        PRIVATE
            ${menu_definition})
endfunction()

add_count_art_library(art_count1_node256
    ART_MENU_COUNT1_NODE256
    ${ART_SOURCES})
add_count_art_library(art_count2_16_256
    ART_MENU_COUNT2_16_256
    ${ART_SOURCES})
add_count_art_library(art_count3_4_16_256
    ART_MENU_COUNT3_4_16_256
    ${ART_SOURCES})
add_count_art_library(art_count4_original4
    ART_MENU_COUNT4_ORIGINAL4
    ${ART_SOURCES})
add_count_art_library(art_count5_4_16_32_64_256
    ART_MENU_COUNT5_4_16_32_64_256
    ${ART_COUNT5_SOURCES})
add_count_art_library(art_count6_4_16_32_48_64_256
    ART_MENU_COUNT6_4_16_32_48_64_256
    ${ART_COUNT5_SOURCES})
add_count_art_library(art_count6_paper6
    ART_MENU_COUNT6_PAPER6
    ${ART_COUNT7_SOURCES})
add_count_art_library(art_count7_2_5_16_32_48_64_256
    ART_MENU_COUNT7_2_5_16_32_48_64_256
    ${ART_COUNT7_SOURCES})

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

add_executable(test_correctness_node32
    tests/test_correctness.c)

target_link_libraries(test_correctness_node32
    PRIVATE
        art_node32)

target_compile_definitions(test_correctness_node32
    PRIVATE
        ART_PROJECT_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
        ART_EXPECT_NODE32)

add_test(NAME test_correctness_node32 COMMAND test_correctness_node32)

add_executable(test_correctness_node64
    tests/test_correctness.c)

target_link_libraries(test_correctness_node64
    PRIVATE
        art_node64)

target_compile_definitions(test_correctness_node64
    PRIVATE
        ART_PROJECT_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
        ART_EXPECT_NODE64)

add_test(NAME test_correctness_node64 COMMAND test_correctness_node64)

add_executable(test_correctness_node32_node64
    tests/test_correctness.c)

target_link_libraries(test_correctness_node32_node64
    PRIVATE
        art_node32_node64)

target_compile_definitions(test_correctness_node32_node64
    PRIVATE
        ART_PROJECT_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
        ART_EXPECT_NODE32
        ART_EXPECT_NODE64)

add_test(NAME test_correctness_node32_node64 COMMAND test_correctness_node32_node64)

add_executable(test_correctness_paper6_indexed
    tests/test_correctness.c)

target_link_libraries(test_correctness_paper6_indexed
    PRIVATE
        art_paper6_indexed)

target_compile_definitions(test_correctness_paper6_indexed
    PRIVATE
        ART_PROJECT_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
        ART_EXPECT_NODE2
        ART_EXPECT_NODE5
        ART_EXPECT_NODE32
        ART_EXPECT_NODE64)

add_test(NAME test_correctness_paper6_indexed COMMAND test_correctness_paper6_indexed)

add_executable(test_correctness_node256_only
    tests/test_correctness.c)

target_link_libraries(test_correctness_node256_only
    PRIVATE
        art_node256_only)

target_compile_definitions(test_correctness_node256_only
    PRIVATE
        ART_PROJECT_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
        ART_EXPECT_NODE256_ONLY)

add_test(NAME test_correctness_node256_only COMMAND test_correctness_node256_only)

function(add_count_correctness target_name library_name)
    add_executable(${target_name}
        tests/test_correctness.c)

    target_link_libraries(${target_name}
        PRIVATE
            ${library_name})

    target_compile_definitions(${target_name}
        PRIVATE
            ART_PROJECT_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
            ${ARGN})

    add_test(NAME ${target_name} COMMAND ${target_name})
endfunction()

add_count_correctness(test_correctness_count1_node256
    art_count1_node256
    ART_EXPECT_NODE256_ONLY)
add_count_correctness(test_correctness_count2_16_256
    art_count2_16_256
    ART_EXPECT_COUNT2_16_256)
add_count_correctness(test_correctness_count3_4_16_256
    art_count3_4_16_256
    ART_EXPECT_COUNT3_4_16_256)
add_count_correctness(test_correctness_count4_original4
    art_count4_original4)
add_count_correctness(test_correctness_count5_4_16_32_64_256
    art_count5_4_16_32_64_256
    ART_EXPECT_NODE32
    ART_EXPECT_NODE64
    ART_EXPECT_COUNT5_4_16_32_64_256)
add_count_correctness(test_correctness_count6_4_16_32_48_64_256
    art_count6_4_16_32_48_64_256
    ART_EXPECT_NODE32
    ART_EXPECT_NODE64)
add_count_correctness(test_correctness_count6_paper6
    art_count6_paper6
    ART_EXPECT_NODE2
    ART_EXPECT_NODE5
    ART_EXPECT_NODE32
    ART_EXPECT_NODE64)
add_count_correctness(test_correctness_count7_2_5_16_32_48_64_256
    art_count7_2_5_16_32_48_64_256
    ART_EXPECT_NODE2
    ART_EXPECT_NODE5
    ART_EXPECT_NODE32
    ART_EXPECT_NODE64
    ART_EXPECT_COUNT7_2_5_16_32_48_64_256)

add_executable(bench_original
    bench/bench_original.c)

target_link_libraries(bench_original
    PRIVATE
        art)
if(NOT MSVC)
    target_link_libraries(bench_original
        PRIVATE
            m)
endif()

target_compile_definitions(bench_original
    PRIVATE
        ART_PROJECT_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\")

add_executable(bench_node32
    bench/bench_original.c)

target_link_libraries(bench_node32
    PRIVATE
        art_node32)
if(NOT MSVC)
    target_link_libraries(bench_node32
        PRIVATE
            m)
endif()

target_compile_definitions(bench_node32
    PRIVATE
        ART_PROJECT_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
        ART_BENCH_VARIANT=\"node32_keyarray\")

add_executable(bench_node64
    bench/bench_original.c)

target_link_libraries(bench_node64
    PRIVATE
        art_node64)
if(NOT MSVC)
    target_link_libraries(bench_node64
        PRIVATE
            m)
endif()

target_compile_definitions(bench_node64
    PRIVATE
        ART_PROJECT_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
        ART_BENCH_VARIANT=\"node64\")

add_executable(bench_node32_node64
    bench/bench_original.c)

target_link_libraries(bench_node32_node64
    PRIVATE
        art_node32_node64)
if(NOT MSVC)
    target_link_libraries(bench_node32_node64
        PRIVATE
            m)
endif()

target_compile_definitions(bench_node32_node64
    PRIVATE
        ART_PROJECT_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
        ART_BENCH_VARIANT=\"node32_node64\")

add_executable(bench_paper6_indexed
    bench/bench_original.c)

target_link_libraries(bench_paper6_indexed
    PRIVATE
        art_paper6_indexed)
if(NOT MSVC)
    target_link_libraries(bench_paper6_indexed
        PRIVATE
            m)
endif()

target_compile_definitions(bench_paper6_indexed
    PRIVATE
        ART_PROJECT_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
        ART_BENCH_VARIANT=\"paper6_indexed\")

add_executable(bench_node256_only
    bench/bench_original.c)

target_link_libraries(bench_node256_only
    PRIVATE
        art_node256_only)
if(NOT MSVC)
    target_link_libraries(bench_node256_only
        PRIVATE
            m)
endif()

target_compile_definitions(bench_node256_only
    PRIVATE
        ART_PROJECT_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
        ART_BENCH_VARIANT=\"node256_only\")

function(add_count_benchmark target_name library_name variant_name)
    add_executable(${target_name}
        bench/bench_original.c)

    target_link_libraries(${target_name}
        PRIVATE
            ${library_name})
    if(NOT MSVC)
        target_link_libraries(${target_name}
            PRIVATE
                m)
    endif()

    target_compile_definitions(${target_name}
        PRIVATE
            ART_PROJECT_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
            ART_BENCH_VARIANT=\"${variant_name}\")
endfunction()

add_count_benchmark(bench_count1_node256
    art_count1_node256
    count1_node256)
add_count_benchmark(bench_count2_16_256
    art_count2_16_256
    count2_node16_node256)
add_count_benchmark(bench_count3_4_16_256
    art_count3_4_16_256
    count3_node4_node16_node256)
add_count_benchmark(bench_count4_original4
    art_count4_original4
    count4_original4)
add_count_benchmark(bench_count5_4_16_32_64_256
    art_count5_4_16_32_64_256
    count5_4_16_32_64_256)
add_count_benchmark(bench_count6_4_16_32_48_64_256
    art_count6_4_16_32_48_64_256
    count6_4_16_32_48_64_256)
add_count_benchmark(bench_count6_paper6
    art_count6_paper6
    count6_paper6)
add_count_benchmark(bench_count7_2_5_16_32_48_64_256
    art_count7_2_5_16_32_48_64_256
    count7_2_5_16_32_48_64_256)

function(add_scan_benchmark target_name art_target variant_name)
    add_executable(${target_name}
        bench/bench_scan.c)

    target_link_libraries(${target_name}
        PRIVATE
            ${art_target})

    target_compile_definitions(${target_name}
        PRIVATE
            ART_PROJECT_SOURCE_DIR=\"${CMAKE_CURRENT_SOURCE_DIR}\"
            ART_BENCH_VARIANT=\"${variant_name}\")
endfunction()

add_scan_benchmark(bench_scan_original art original4)
add_scan_benchmark(bench_scan_node64 art_node64 node64)
add_scan_benchmark(bench_scan_node32_node64 art_node32_node64 node32_node64)
add_scan_benchmark(bench_scan_paper6_indexed art_paper6_indexed paper6_indexed)
add_scan_benchmark(bench_scan_node256_only art_node256_only node256_only)

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

    add_executable(test_art_upstream_node32
        tests/runner.c)

    target_include_directories(test_art_upstream_node32
        PRIVATE
            ${CHECK_INCLUDE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/src)

    target_link_libraries(test_art_upstream_node32
        PRIVATE
            art_node32
            ${CHECK_LIBRARY})

    target_compile_definitions(test_art_upstream_node32
        PRIVATE
            ART_MENU_NODE32_ARRAY)

    add_test(NAME test_art_upstream_node32 COMMAND test_art_upstream_node32)
    set_tests_properties(test_art_upstream_node32
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(test_art_upstream_node64
        tests/runner.c)

    target_include_directories(test_art_upstream_node64
        PRIVATE
            ${CHECK_INCLUDE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/src)

    target_link_libraries(test_art_upstream_node64
        PRIVATE
            art_node64
            ${CHECK_LIBRARY})

    target_compile_definitions(test_art_upstream_node64
        PRIVATE
            ART_MENU_NODE64_INDEXED)

    add_test(NAME test_art_upstream_node64 COMMAND test_art_upstream_node64)
    set_tests_properties(test_art_upstream_node64
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(test_art_upstream_node32_node64
        tests/runner.c)

    target_include_directories(test_art_upstream_node32_node64
        PRIVATE
            ${CHECK_INCLUDE_DIR}
            ${CMAKE_CURRENT_SOURCE_DIR}/src)

    target_link_libraries(test_art_upstream_node32_node64
        PRIVATE
            art_node32_node64
            ${CHECK_LIBRARY})

    target_compile_definitions(test_art_upstream_node32_node64
        PRIVATE
            ART_MENU_NODE32_NODE64_INDEXED)

    add_test(NAME test_art_upstream_node32_node64 COMMAND test_art_upstream_node32_node64)
    set_tests_properties(test_art_upstream_node32_node64
        PROPERTIES
            WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    function(add_upstream_check_variant target_name library_name menu_definition)
        add_executable(${target_name}
            tests/runner.c)

        target_include_directories(${target_name}
            PRIVATE
                ${CHECK_INCLUDE_DIR}
                ${CMAKE_CURRENT_SOURCE_DIR}/src)

        target_link_libraries(${target_name}
            PRIVATE
                ${library_name}
                ${CHECK_LIBRARY})

        target_compile_definitions(${target_name}
            PRIVATE
                ${menu_definition})

        add_test(NAME ${target_name} COMMAND ${target_name})
        set_tests_properties(${target_name}
            PROPERTIES
                WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    endfunction()

    add_upstream_check_variant(test_art_upstream_paper6_indexed
        art_paper6_indexed
        ART_MENU_PAPER6_INDEXED)
    add_upstream_check_variant(test_art_upstream_node256_only
        art_node256_only
        ART_MENU_NODE256_ONLY)
endif()
