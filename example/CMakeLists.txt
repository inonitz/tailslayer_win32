cmake_minimum_required(VERSION 3.16)
project(tailslayer_example C CXX)


set(SOURCES
    tailslayer_example.cpp
)


add_executable(${PROJECT_NAME} ${SOURCES})
target_compile_features(${PROJECT_NAME} INTERFACE 
    c_std_11
    cxx_std_17
)
target_link_libraries(${PROJECT_NAME} PRIVATE 
    tailslayer
)
