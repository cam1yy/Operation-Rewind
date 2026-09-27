# CMake build for the reconstructed GFSDK_Aftermath_Lib.x64.dll.
#
# The Visual Studio solution (GFSDK_Aftermath_Lib.sln) is the primary build; this file is
# here for command line builds and CI:
#
#   cmake -S . -B build -G "Visual Studio 17 2022" -A x64
#   cmake --build build --config Release
#
# The result is build/Release/GFSDK_Aftermath_Lib.x64.dll with the ordinals from
# exports.def.

cmake_minimum_required(VERSION 3.20)

project(GFSDK_Aftermath_Lib
        VERSION 1.3.0
        DESCRIPTION "Reconstructed NVIDIA Nsight Aftermath shim (GFSDK_Aftermath_Lib.x64.dll)"
        LANGUAGES CXX)

if(NOT WIN32)
    message(FATAL_ERROR
            "This project rebuilds a Windows DLL and needs MSVC (v143) plus the Windows SDK.\n"
            "On other hosts use tests/syntax_check.sh, which type checks the sources against "
            "a stub Win32 header set.")
endif()

add_library(GFSDK_Aftermath_Lib SHARED
    src/aftermath_api.cpp
    src/dllmain.cpp
    src/loader/nv_driver_store.cpp
    src/loader/nv_module_loader.cpp
    src/loader/nv_path_utils.cpp
    src/loader/nv_strsafe.cpp
    src/loader/nv_system_api.cpp
    src/nvapi/nvapi_bridge.cpp

    include/GFSDK_Aftermath.h
    src/aftermath_internal.h
    src/compat/com_min.h
    src/compat/ida_defs.h
    src/loader/nv_driver_store.h
    src/loader/nv_module_loader.h
    src/loader/nv_path_utils.h
    src/loader/nv_strsafe.h
    src/loader/nv_system_api.h
    src/nvapi/nvapi_bridge.h
    src/nvapi/nvapi_types.h

    exports.def
)

target_include_directories(GFSDK_Aftermath_Lib
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(GFSDK_Aftermath_Lib PRIVATE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WINDOWS
    _USRDLL
    GFSDK_AFTERMATH_BUILD_DLL
    _CRT_SECURE_NO_WARNINGS
)

set_target_properties(GFSDK_Aftermath_Lib PROPERTIES
    OUTPUT_NAME "GFSDK_Aftermath_Lib.x64"
    CXX_STANDARD 14
    CXX_STANDARD_REQUIRED ON
)

# Only kernel32: every other Windows API this DLL uses is resolved with GetProcAddress.
target_link_libraries(GFSDK_Aftermath_Lib PRIVATE kernel32)

if(MSVC)
    target_compile_options(GFSDK_Aftermath_Lib PRIVATE /W4 /permissive- /GR- /EHs-c-)
    # Static CRT, like the original binary.
    set_property(TARGET GFSDK_Aftermath_Lib PROPERTY
                 MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
