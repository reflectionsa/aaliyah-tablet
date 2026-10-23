# An Echo VR mod: a nEVR plugin DLL. Its name, file, version and capabilities all come from
# echo-mod.json, so renaming the mod or bumping its version is an edit there only.
#
#   cmake --preset mingw          (Linux/macOS with MinGW-w64, or MSYS2 on Windows)
#   cmake --build --preset mingw  -> build/mingw/<file>.dll
#
# MSVC or clang-cl work too (preset "msvc"): the C runtime is linked statically (/MT).

cmake_minimum_required(VERSION 3.20)

# ---- echo-mod.json ----
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/echo-mod.json" MOD_JSON)
string(JSON MOD_ID GET "${MOD_JSON}" id)
string(JSON MOD_NAME GET "${MOD_JSON}" name)
string(JSON MOD_SUMMARY GET "${MOD_JSON}" summary)
string(JSON MOD_VERSION GET "${MOD_JSON}" version)
string(JSON MOD_FILE GET "${MOD_JSON}" file)
string(JSON MOD_API GET "${MOD_JSON}" api)

# A semantic version (semver.org): MAJOR.MINOR.PATCH, then an optional -prerelease and +build.
if(NOT MOD_VERSION MATCHES "^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)(-[0-9A-Za-z.-]+)?(\\+[0-9A-Za-z.-]+)?$")
    message(FATAL_ERROR "echo-mod.json: version \"${MOD_VERSION}\" is not a semantic version (see semver.org)")
endif()
set(MOD_VERSION_MAJOR ${CMAKE_MATCH_1})
set(MOD_VERSION_MINOR ${CMAKE_MATCH_2})
set(MOD_VERSION_PATCH ${CMAKE_MATCH_3})

if(NOT MOD_FILE MATCHES "^([A-Za-z0-9._-]+)\\.dll$")
    message(FATAL_ERROR "echo-mod.json: file \"${MOD_FILE}\" must be a plain name ending in .dll")
endif()
set(MOD_STEM ${CMAKE_MATCH_1})

# capabilities -> NEVR_PLUGIN_CAP_* bits
set(MOD_CAPS 0)
string(JSON caps_count LENGTH "${MOD_JSON}" capabilities)
if(caps_count GREATER 0)
    math(EXPR caps_last "${caps_count} - 1")
    foreach(i RANGE ${caps_last})
        string(JSON cap GET "${MOD_JSON}" capabilities ${i})
        if(cap STREQUAL "observes-only")
            math(EXPR MOD_CAPS "${MOD_CAPS} | 0x01")
        elseif(cap STREQUAL "cosmetic")
            math(EXPR MOD_CAPS "${MOD_CAPS} | 0x02")
        elseif(cap STREQUAL "alters-gameplay")
            math(EXPR MOD_CAPS "${MOD_CAPS} | 0x04")
        elseif(cap STREQUAL "alters-rules")
            math(EXPR MOD_CAPS "${MOD_CAPS} | 0x08")
        elseif(cap STREQUAL "network")
            math(EXPR MOD_CAPS "${MOD_CAPS} | 0x10")
        elseif(cap STREQUAL "hooks-engine")
            math(EXPR MOD_CAPS "${MOD_CAPS} | 0x20")
        else()
            message(FATAL_ERROR "echo-mod.json: unknown capability \"${cap}\"")
        endif()
    endforeach()
endif()

project(${MOD_STEM} VERSION ${MOD_VERSION_MAJOR}.${MOD_VERSION_MINOR}.${MOD_VERSION_PATCH} LANGUAGES C CXX)
message(STATUS "${MOD_NAME} ${MOD_VERSION} -> ${MOD_FILE} (capabilities ${MOD_CAPS})")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# MinHook for detours: off until the mod needs it (also add "hooks-engine" to capabilities).
option(ECHO_MOD_MINHOOK "Build with MinHook for detours" OFF)

add_library(mod SHARED
    src/mod.cpp
    src/plugin.cpp
    src/mod.h
    src/log.cpp
    src/args.cpp
    src/game.cpp
    src/exports.def
)
target_include_directories(mod PRIVATE include src)
target_compile_definitions(mod PRIVATE
    MOD_ID="${MOD_ID}"
    MOD_STEM="${MOD_STEM}"
    MOD_SUMMARY="${MOD_SUMMARY}"
    MOD_VERSION="${MOD_VERSION}"
    MOD_VERSION_MAJOR=${MOD_VERSION_MAJOR}
    MOD_VERSION_MINOR=${MOD_VERSION_MINOR}
    MOD_VERSION_PATCH=${MOD_VERSION_PATCH}
    MOD_API=${MOD_API}
    MOD_CAPABILITIES=${MOD_CAPS}
    WIN32_LEAN_AND_MEAN
    NOMINMAX
)
target_link_libraries(mod PRIVATE shell32)
set_target_properties(mod PROPERTIES OUTPUT_NAME "${MOD_STEM}" PREFIX "")

if(ECHO_MOD_MINHOOK)
    # Pinned: the v1.3.4 release tarball, checked by hash.
    include(FetchContent)
    FetchContent_Declare(minhook
        URL https://github.com/TsudaKageyu/minhook/archive/refs/tags/v1.3.4.tar.gz
        URL_HASH SHA256=1aebeae4ca898330c507860acc2fca2eb335fe446a3a2b8444c3bf8b2660a14e
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR no-cmake-project
    )
    FetchContent_MakeAvailable(minhook)
    add_library(minhook STATIC
        ${minhook_SOURCE_DIR}/src/buffer.c
        ${minhook_SOURCE_DIR}/src/hook.c
        ${minhook_SOURCE_DIR}/src/trampoline.c
        ${minhook_SOURCE_DIR}/src/hde/hde64.c
    )
    target_include_directories(minhook PUBLIC ${minhook_SOURCE_DIR}/include)
    target_sources(mod PRIVATE src/hooks.cpp src/hooks.h)
    target_link_libraries(mod PRIVATE minhook)
    target_compile_definitions(mod PRIVATE ECHO_MOD_MINHOOK=1)
endif()

if(MSVC)
    target_compile_options(mod PRIVATE /W4 /utf-8)
else()
    target_compile_options(mod PRIVATE -Wall -Wextra)
    # Only what exports.def lists leaves the DLL.
    target_link_options(mod PRIVATE -Wl,--exclude-all-symbols)
endif()

# The smoke host (tests/smoke_host.c) loads the DLL the way nEVR does and calls its exports.
add_executable(smoke_host tests/smoke_host.c)
set_target_properties(smoke_host PROPERTIES OUTPUT_NAME "smoke_host")
if(NOT MSVC)
    target_link_options(smoke_host PRIVATE -municode)
endif()
