cmake_minimum_required(VERSION 3.16)
project(tailslayer_probe C CXX)


set(HEADERS
    trefi_probe.hpp
)

set(SOURCES
    trefi_probe.cpp
)


add_executable(${PROJECT_NAME})
target_sources(${PROJECT_NAME} PRIVATE ${SOURCES} ${HEADERS})
target_compile_features(${PROJECT_NAME} INTERFACE 
    c_std_11
    cxx_std_17
)
target_link_libraries(${PROJECT_NAME} PRIVATE 
    TAILSLAYER::tailslayer
    cxxopts::cxxopts
)
