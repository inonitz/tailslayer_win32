cmake_minimum_required(VERSION 3.14)
include(FetchContent)
project(MemProcFSBinaries)


# 1. Fetch the platform specific release
set(MEMPROC_GITHUB_REPO_NAME "ufrisk/MemProcFS")
set(MEMPROC_VERSION_TAG "v5.17")
set(MEMPRO_GITHUB_API_URL "https://api.github.com/repos/${MEMPROC_GITHUB_REPO_NAME}/releases/tags/${MEMPROC_VERSION_TAG}")
set(MEMPROC_RELEASE_JSON "${CMAKE_BINARY_DIR}/release_info.json")
set(MEMPROC_FOUND_PLATFORM_URL "")

# Platform Specific Regex Matchers
if(APPLE)
    set(MEMPROC_PLATFORM_SEARCH_REGEX ".*mac.*\\.tar\\.gz")
elseif(WIN32)
    set(MEMPROC_PLATFORM_SEARCH_REGEX ".*win.*x64.*\\.zip")
elseif(UNIX)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64")
        set(MEMPROC_PLATFORM_SEARCH_REGEX ".*linux.*(aarch64|arm64).*\\.tar\\.gz")
    else()
        set(MEMPROC_PLATFORM_SEARCH_REGEX ".*linux.*(x64|x86_64|amd64).*\\.tar\\.gz")
    endif()
endif()


# Fetch Metadata
if(NOT EXISTS ${MEMPROC_RELEASE_JSON})
    message(STATUS "Fetching metadata from GitHub...")
    file(DOWNLOAD "${MEMPRO_GITHUB_API_URL}" "${MEMPROC_RELEASE_JSON}"
        HTTPHEADER "Accept: application/vnd.github+json"
        HTTPHEADER "User-Agent: CMake-Fetch-Script"
        STATUS DOWNLOAD_STATUS
    )
    
    list(GET DOWNLOAD_STATUS 0 STATUS_CODE)
    if(NOT STATUS_CODE EQUAL 0)
        message(FATAL_ERROR "Failed to connect to GitHub API: ${DOWNLOAD_STATUS}")
    endif()
endif()

# Extract matching URL from Metadata
file(READ "${MEMPROC_RELEASE_JSON}" MEMPROC_RELEASE_JSON)
string(REGEX MATCHALL "https://github.com/[^\"]+" ASSET_URLS "${MEMPROC_RELEASE_JSON}")
foreach(URL IN LISTS ASSET_URLS)
    if(URL MATCHES "${MEMPROC_PLATFORM_SEARCH_REGEX}")
        set(MEMPROC_FOUND_PLATFORM_URL "${URL}")
        break()
    endif()
endforeach()

# Download the content
if(MEMPROC_FOUND_PLATFORM_URL)
    message(STATUS "Found matching asset: ${MEMPROC_FOUND_PLATFORM_URL}")
    
    FetchContent_Declare(
        memprocfs_binaries
        URL "${MEMPROC_FOUND_PLATFORM_URL}"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
    
    FetchContent_MakeAvailable(memprocfs_binaries)
    
    message(STATUS "MemProcFS source dir: ${memprocfs_binaries_SOURCE_DIR}")
else()
    message(FATAL_ERROR "Could not find a release asset matching the regex: ${MEMPROC_PLATFORM_SEARCH_REGEX}")
endif()




# 2. Define the Interface Library
# This 'MEMPROCFS::memprocfs' target is what the rest of your project will use.
set(MEMPROC_INTERFACE_LIBRARY_NAME
    vmm_interface
)

set(MEMPROC_DIRECTORY
    ${memprocfs_binaries_SOURCE_DIR}
)
set(MEMPROC_EXPLICIT_IMPORT_BINARIES
)
set(MEMPROC_EXPLICIT_BINARIES
)


message(${MEMPROC_DIRECTORY})


if(WIN32)
    set(MEMPROC_EXPLICIT_IMPORT_BINARIES "${MEMPROC_DIRECTORY}/vmm.lib"
        CACHE INTERNAL "MemProc Import Binaries"
    )
    set(MEMPROC_EXPLICIT_BINARIES 
        "${MEMPROC_DIRECTORY}/vmm.dll"
        "${MEMPROC_DIRECTORY}/LeechCore.dll"
        "${MEMPROC_DIRECTORY}/winpmem_x64.sys"
        "${MEMPROC_DIRECTORY}/info.db"
        CACHE INTERNAL "MemProc binaries"
    )

    add_library(memproc_vmm_lib UNKNOWN IMPORTED)
    set_target_properties(memproc_vmm_lib PROPERTIES 
        IMPORTED_LOCATION "${MEMPROC_DIRECTORY}/vmm.lib"
    )
    set(MEMPROC_LINK_TARGETS memproc_vmm_lib)
elseif(UNIX)
    # set(MEMPROC_EXPLICIT_IMPORT_BINARIES "${MEMPROC_DIRECTORY}/vmm.a"
    #     CACHE INTERNAL "MemProc Static import library"
    # )
    set(MEMPROC_EXPLICIT_IMPORT_BINARIES
        "${MEMPROC_DIRECTORY}/vmm.so"
        "${MEMPROC_DIRECTORY}/leechcore.so"
        CACHE INTERNAL "MemProc Static import library"
    )
    set(MEMPROC_EXPLICIT_BINARIES 
        "${MEMPROC_DIRECTORY}/vmm.so"
        "${MEMPROC_DIRECTORY}/leechcore.so"
        # https://github.com/ufrisk/MemProcFS/wiki/_Linux | ufrisk explains: 
        # "info.db is an sqlite database which contains common type and symbol offsets required for some tasks."
        "${MEMPROC_DIRECTORY}/info.db"
        CACHE INTERNAL "MemProc binaries"
    )

    add_library(memproc_vmm_so SHARED IMPORTED)
    set_target_properties(memproc_vmm_so PROPERTIES 
        IMPORTED_LOCATION "${MEMPROC_DIRECTORY}/vmm.so"
    )
    
    # Define LeechCore
    add_library(memproc_leech_so SHARED IMPORTED)
    set_target_properties(memproc_leech_so PROPERTIES 
        IMPORTED_LOCATION "${MEMPROC_DIRECTORY}/leechcore.so"
    )


    set(MEMPROC_LINK_TARGETS memproc_vmm_so memproc_leech_so)
else()
#
endif()


add_library(${MEMPROC_INTERFACE_LIBRARY_NAME} INTERFACE)
add_library(MEMPROCFS::memprocfs ALIAS ${MEMPROC_INTERFACE_LIBRARY_NAME})
target_link_libraries(${MEMPROC_INTERFACE_LIBRARY_NAME} INTERFACE ${MEMPROC_LINK_TARGETS})
target_include_directories(${MEMPROC_INTERFACE_LIBRARY_NAME} INTERFACE "${MEMPROC_DIRECTORY}/")


function(target_deploy_memprocfs _target)
    add_custom_command(TARGET ${_target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different 
        ${MEMPROC_EXPLICIT_BINARIES} $<TARGET_FILE_DIR:${_target}>

        VERBATIM
        COMMAND_EXPAND_LISTS
        COMMENT "Deploying MemProcFS & WinPmem Binaries to $<TARGET_FILE_DIR:${_target}>"
    )
endfunction()