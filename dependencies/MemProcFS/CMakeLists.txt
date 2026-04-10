cmake_minimum_required(VERSION 3.14)
include(FetchContent)
project(MemProcFSBinaries)


# --- MemProcFS Configuration (Windows Only for now) ---
set(MEM_VERSION "5.17")
set(MEM_URL 
    https://github.com/ufrisk/MemProcFS/releases/download/v${MEM_VERSION}/MemProcFS_files_and_binaries-win_x64-latest.zip
)
set(MEM_HASH 
    "SHA256=7d6fc13e1c77e05e9707f2cbfa0c20c6e50271e8dfcc1881bd6e6916a164b9be"
)
FetchContent_Declare(
    memprocfs_binaries
    URL ${MEM_URL}
    URL_HASH ${MEM_HASH}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)


# Download and extract
FetchContent_MakeAvailable(memprocfs_binaries)


# 2. Define the Interface Library
# This 'MEMPROCFS::memprocfs' target is what the rest of your project will use.
set(MEMPROC_INTERFACE_LIBRARY_NAME
    vmm_interface
)

set(MEMPROC_DIRECTORY
    ${memprocfs_binaries_SOURCE_DIR}
)


message(${MEMPROC_DIRECTORY})


if(WIN32)
    set(MEMPROC_EXPLICIT_IMPORT_BINARIES "${MEMPROC_DIRECTORY}/vmm.lib")
    set(MEMPROC_EXPLICIT_BINARIES 
        "${MEMPROC_DIRECTORY}/vmm.dll"
        "${MEMPROC_DIRECTORY}/LeechCore.dll"
        "${MEMPROC_DIRECTORY}/winpmem_x64.sys"
    )
else()
    set(MEMPROC_EXPLICIT_IMPORT_BINARIES "${MEMPROC_DIRECTORY}/vmm.so")
    set(MEMPROC_EXPLICIT_BINARIES 
        "${MEMPROC_DIRECTORY}/vmm.so"
        "${MEMPROC_DIRECTORY}/LeechCore.so"
    )
endif()

add_library(memprocfs_raw_prebuilt UNKNOWN IMPORTED)
set_target_properties(memprocfs_raw_prebuilt PROPERTIES
    IMPORTED_LOCATION ${MEMPROC_EXPLICIT_IMPORT_BINARIES}
    INTERFACE_INCLUDE_DIRECTORIES "${MEMPROC_DIRECTORY}/"
)


add_library(${MEMPROC_INTERFACE_LIBRARY_NAME} INTERFACE)
add_library(MEMPROCFS::memprocfs ALIAS ${MEMPROC_INTERFACE_LIBRARY_NAME})
target_link_libraries(${MEMPROC_INTERFACE_LIBRARY_NAME} INTERFACE memprocfs_raw_prebuilt)
target_include_directories(${MEMPROC_INTERFACE_LIBRARY_NAME} INTERFACE "${MEMPROC_DIRECTORY}/")


function(target_deploy_memprocfs _target)
    add_custom_command(TARGET ${_target} POST_BUILD
        COMMAND 
            ${CMAKE_COMMAND} -E copy_if_different 
            ${MEMPROC_EXPLICIT_BINARIES} 
            $<TARGET_FILE_DIR:${_target}>
        COMMENT "Deploying MemProcFS/WinPmem binaries to $<TARGET_FILE_DIR:${_target}>"
    )
endfunction()