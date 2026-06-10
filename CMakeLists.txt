cmake_minimum_required(VERSION 3.26)
project(sage LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SAGE_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Dependencies
if (NOT TARGET EnTT::EnTT)
    include(FetchContent)
    FetchContent_Declare(
            entt
            GIT_REPOSITORY "https://github.com/skypjack/entt.git"
            GIT_TAG "v3.13.2"
    )
    FetchContent_MakeAvailable(entt)
endif ()

if (NOT TARGET lua_static)
    add_subdirectory(vendor/lua)
endif ()

set(PLATFORM "Desktop" CACHE STRING "" FORCE)
if (NOT TARGET raylib)
    add_subdirectory(vendor/raylib)
    # raylib's DecompressData() default-caps output at 64 MiB (rcore.c). Packed
    # assets can exceed that, and raylib realloc()s the transient buffer down.
    target_compile_definitions(raylib PRIVATE MAX_DECOMPRESSION_SIZE=512)
endif ()

# Options
option(SAGE_BUILD_EDITOR "Build the Sage editor" OFF)

# Tell the engine where to find project-specific extension headers
# (e.g. project/CustomCollisionLayers.hpp).
if (NOT DEFINED SAGE_PROJECT_INCLUDE_DIR)
    set(SAGE_PROJECT_INCLUDE_DIR ${SAGE_ROOT_DIR}/engine/defaults)
endif ()

# Add subdirectories
add_subdirectory(engine)

if (SAGE_BUILD_EDITOR)
    add_subdirectory(editor)
endif ()
