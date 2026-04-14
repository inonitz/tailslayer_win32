cmake_minimum_required(VERSION 3.16)
project(tailslayer_workspace C CXX)


set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)


add_subdirectory(dependencies/util2)
add_subdirectory(dependencies/cxxopts)
add_subdirectory(dependencies/MemProcFS)
add_subdirectory(tailslayer)
add_subdirectory(probe)
add_subdirectory(benchmark)
add_subdirectory(example)
add_subdirectory(sandbox)

