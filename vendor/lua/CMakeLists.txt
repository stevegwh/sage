# Lua 5.4.8 (https://www.lua.org) built as a static library for embedding.
# Sources are the unmodified upstream src/ tree minus the lua/luac
# standalone-interpreter entry points.

file(GLOB LUA_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/*.c)

add_library(lua_static STATIC ${LUA_SOURCES})

target_include_directories(lua_static PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

if (UNIX)
    # Enables system dependent features (e.g. os/io libs use POSIX APIs).
    target_compile_definitions(lua_static PRIVATE LUA_USE_POSIX)
endif ()
