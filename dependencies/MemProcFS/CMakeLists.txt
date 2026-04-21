cmake_minimum_required(VERSION 3.14)
include(FetchContent)
project(MemProcFSBinaries)


# --- MemProcFS Configuration (Windows Only for now) ---
set(MEM_VERSION "5.17")
set(MEM_URL 
    https://github.com/ufrisk/MemProcFS/releases/download/v${MEM_VERSION}/MemProcFS_files_and_binaries-win_x64-latest.zip
)
set(MEM_HASH 
    "SHA256=7da521d7fd9b7de28bf5a9420d9d97a2b25ebdb2ce58a070104225bd07b455d9"
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
elseif(UNIX)
    set(MEMPROC_EXPLICIT_IMPORT_BINARIES "${MEMPROC_DIRECTORY}/vmm.a"
        CACHE INTERNAL "MemProc Static import library"
    )
    set(MEMPROC_EXPLICIT_BINARIES 
        "${MEMPROC_DIRECTORY}/vmm.so"
        "${MEMPROC_DIRECTORY}/LeechCore.so"
        # https://github.com/ufrisk/MemProcFS/wiki/_Linux | ufrisk explains: 
        # "info.db is an sqlite database which contains common type and symbol offsets required for some tasks."
        "${MEMPROC_DIRECTORY}/info.db"
        CACHE INTERNAL "MemProc binaries"
    )
else()
#
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
        COMMAND ${CMAKE_COMMAND} -E copy_if_different 
        ${MEMPROC_EXPLICIT_BINARIES} $<TARGET_FILE_DIR:${_target}>

        VERBATIM
        COMMAND_EXPAND_LISTS
        COMMENT "Deploying MemProcFS & WinPmem Binaries to $<TARGET_FILE_DIR:${_target}>"
    )
endfunction()