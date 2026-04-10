cmake_minimum_required(VERSION 3.16)
project(tailslayer_benchmark C CXX)


set(BENCHMARK_HEADERS
    include/benchmark/app_config.hpp
    include/benchmark/benchmark.hpp
    include/benchmark/hw_utils.hpp
    include/benchmark/stats.hpp
)

set(BENCHMARK_SOURCES
    source/app_config.cpp
    source/benchmark.cpp
    source/stats.cpp
    source/main.cpp
)


set(IS_GNU_OR_CLANG $<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>)
set(IS_MSVC         $<CXX_COMPILER_ID:MSVC>)


add_executable(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE ${BENCHMARK_HEADERS})
target_sources(${PROJECT_NAME} PRIVATE ${BENCHMARK_SOURCES})
target_link_libraries(${PROJECT_NAME} PRIVATE
    UTIL2::util2
    TAILSLAYER::tailslayer
    cxxopts::cxxopts
    MEMPROCFS::memprocfs
)
target_deploy_memprocfs(${PROJECT_NAME})
# target_sources(${PROJECT_NAME} INTERFACE 
#     FILE_SET ${PROJECT_NAME}_headeronly
#     TYPE HEADERS
#     BASE_DIRS include
#     FILES ${HEADER_ONLY_LIB_HEADERS}
# )
target_include_directories(${PROJECT_NAME} PRIVATE include)
target_compile_features(${PROJECT_NAME} PRIVATE 
    c_std_11
    cxx_std_17
)
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<IF:${IS_GNU_OR_CLANG},_GNU_SOURCE,>
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    INTERFACE_CXX_STANDARD_REQUIRED      ON
    INTERFACE_CXX_EXTENSIONS             ON
    INTERFACE_CXX_VISIBILITY_PRESET      hidden
    INTERFACE_VISIBILITY_INLINES_HIDDEN  ON
    VERIFY_INTERFACE_HEADER_SETS ON
)

