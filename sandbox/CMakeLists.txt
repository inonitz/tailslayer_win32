cmake_minimum_required(VERSION 3.16)
project(tailslayer_sandbox C CXX)


set(SOURCES
    tailslayer_alloc.cpp
)


add_executable(${PROJECT_NAME} ${SOURCES})
target_compile_features(${PROJECT_NAME} INTERFACE 
    c_std_11
    cxx_std_17
)
target_link_libraries(${PROJECT_NAME} PRIVATE 
    TAILSLAYER::tailslayer
    MEMPROCFS::memprocfs
    UTIL2::util2
)
