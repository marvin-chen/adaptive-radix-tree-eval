cmake_minimum_required(VERSION 3.20)
project(art_ablation CXX)
set(CMAKE_CXX_STANDARD 20)

# Always compile with full optimization — results are meaningless without it
set(CMAKE_BUILD_TYPE Release)
add_compile_options(-O3 -march=native)

add_library(art src/art.cpp)
target_include_directories(art PUBLIC include/)

add_executable(bench bench/bench.cpp)
target_link_libraries(bench art)