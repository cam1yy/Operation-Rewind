# =============================================================================
#  CMakeLists.txt
# -----------------------------------------------------------------------------
#  Alternative to the Visual Studio project in build/.  Both build the same six
#  translation units and the same export table; this one additionally works
#  with a MinGW cross toolchain, which is useful for checking that nothing in
#  the tree depends on a GCC-only or MSVC-only extension.
#
#  MSVC (from a Developer Command Prompt):
#      cmake -B out -A x64 -DCMAKE_BUILD_TYPE=Release
#      cmake --build out --config Release
#
#  MinGW cross compile (syntax/link check only):
#      cmake -B out -DCMAKE_SYSTEM_NAME=Windows \
#            -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++
#      cmake --build out
#
#  Host verification build (the behavioural test; see tests/ and Makefile.host):
#      cmake -B out-host -DAFTERMATH_BUILD_HOST_TEST=ON
#      cmake --build out-host && ctest --test-dir out-host
# =============================================================================

cmake_minimum_required(VERSION 3.16)
project(GFSDK_Aftermath_Lib CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(AFTERMATH_BUILD_HOST_TEST
       "Build the host-side behavioural test instead of the DLL" OFF)

set(AFTERMATH_SOURCES
    src/aftermath_globals.cpp
    src/aftermath_core.cpp
    src/aftermath_exports.cpp
    src/dllmain.cpp
    src/nvapi/nvapi_loader.cpp
    src/nvapi/aftermath_driver.cpp
)

if(AFTERMATH_BUILD_HOST_TEST)

    add_executable(host_smoke_test
        ${AFTERMATH_SOURCES}
        tests/host_smoke_test.cpp
    )
    target_compile_definitions(host_smoke_test PRIVATE AFTERMATH_TEST_BUILD)
    target_include_directories(host_smoke_test PRIVATE include src)
    target_compile_options(host_smoke_test PRIVATE
        $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Werror -Wno-unused-parameter>)

    enable_testing()
    add_test(NAME host_smoke_test COMMAND host_smoke_test)

    # The public headers must also compile as plain C.
    enable_language(C)
    add_library(public_headers_c_compat OBJECT tests/public_headers_c_compat.c)
    target_include_directories(public_headers_c_compat PRIVATE include)
    set_target_properties(public_headers_c_compat PROPERTIES
        C_STANDARD 99 C_STANDARD_REQUIRED ON)

else()

    add_library(GFSDK_Aftermath_Lib SHARED ${AFTERMATH_SOURCES})

    target_include_directories(GFSDK_Aftermath_Lib PRIVATE include src)
    target_compile_definitions(GFSDK_Aftermath_Lib PRIVATE GFSDK_AFTERMATH_EXPORTS)

    # The export table is authoritative; use it on every toolchain that accepts
    # a .def file.
    if(MSVC OR MINGW)
        target_sources(GFSDK_Aftermath_Lib PRIVATE src/exports.def)
        if(MSVC)
            set_target_properties(GFSDK_Aftermath_Lib PROPERTIES
                LINK_FLAGS "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/src/exports.def")
        else()
            set_target_properties(GFSDK_Aftermath_Lib PROPERTIES
                LINK_FLAGS "${CMAKE_CURRENT_SOURCE_DIR}/src/exports.def")
        endif()
    endif()

    set_target_properties(GFSDK_Aftermath_Lib PROPERTIES
        OUTPUT_NAME "GFSDK_Aftermath_Lib.x64"
        PREFIX ""
    )

    if(MSVC)
        # Match the original image: static CRT, no RTTI, no exceptions.
        target_compile_options(GFSDK_Aftermath_Lib PRIVATE /W4)
        set_property(TARGET GFSDK_Aftermath_Lib PROPERTY
            MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        target_compile_options(GFSDK_Aftermath_Lib PRIVATE
            /GR- /EHs-c- /GS)
    else()
        target_compile_options(GFSDK_Aftermath_Lib PRIVATE
            -Wall -Wextra -Wno-unused-parameter
            -fno-exceptions -fno-rtti)
    endif()

endif()
