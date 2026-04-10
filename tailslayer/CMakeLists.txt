cmake_minimum_required(VERSION 3.16)
find_package(Threads REQUIRED)
project(tailslayer C CXX)


set(HEADER_ONLY_LIB_HEADERS
    include/tailslayer/hedged_reader.hpp
    include/tailslayer/utilities.hpp

)


add_library(${PROJECT_NAME} INTERFACE)
add_library(TAILSLAYER::tailslayer ALIAS ${PROJECT_NAME})
target_sources(${PROJECT_NAME} INTERFACE 
    FILE_SET ${PROJECT_NAME}_headeronly
    TYPE HEADERS
    BASE_DIRS include
    FILES ${HEADER_ONLY_LIB_HEADERS}
)
target_include_directories(${PROJECT_NAME} INTERFACE include)
target_compile_features(${PROJECT_NAME} INTERFACE 
    c_std_11
    cxx_std_17
)

set_target_properties(${PROJECT_NAME} PROPERTIES
    INTERFACE_CXX_STANDARD_REQUIRED      ON
    INTERFACE_CXX_EXTENSIONS             OFF
    INTERFACE_CXX_VISIBILITY_PRESET      hidden
    INTERFACE_VISIBILITY_INLINES_HIDDEN  ON
    VERIFY_INTERFACE_HEADER_SETS         ON
)
target_link_libraries(${PROJECT_NAME} INTERFACE
    Threads::Threads
    UTIL2::util2
    $<$<PLATFORM_ID:Windows>:onecore>
)

