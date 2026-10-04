# Streaming comparison demo.
#
# Build from the repository root:
#   cmake -S tools -B build/tools
#   cmake --build build/tools
#
# Or compile directly:
#   g++ -std=c++17 -I include tools/stream_compare_demo.cpp -o stream_compare_demo

cmake_minimum_required(VERSION 3.13)
project(stream_compare_demo LANGUAGES CXX)

add_executable(stream_compare_demo stream_compare_demo.cpp)
target_include_directories(stream_compare_demo PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../include)
target_compile_features(stream_compare_demo PRIVATE cxx_std_11)
