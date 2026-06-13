# editor/CMakeLists.txt

if (NOT DEFINED SAGE_ROOT_DIR)
    get_filename_component(SAGE_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)
endif ()

file(GLOB EDITOR_HEADERS *.hpp)
file(GLOB EDITOR_SOURCES *.cpp)

# main.cpp is the standalone-editor entry point; keep it out of the library so
# the game project can provide its own entry point that registers the lq game
# runtime (play-in-editor).
list(REMOVE_ITEM EDITOR_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

set(IMGUI_DIR ${SAGE_ROOT_DIR}/vendor/imgui)

add_library(imgui_raylib STATIC
        ${IMGUI_DIR}/imgui.cpp
        ${IMGUI_DIR}/imgui_demo.cpp
        ${IMGUI_DIR}/imgui_draw.cpp
        ${IMGUI_DIR}/imgui_tables.cpp
        ${IMGUI_DIR}/imgui_widgets.cpp
        ${IMGUI_DIR}/misc/cpp/imgui_stdlib.cpp
        ${IMGUI_DIR}/backends/rlImGui.cpp
)

target_include_directories(imgui_raylib
        PUBLIC
        ${IMGUI_DIR}
        ${IMGUI_DIR}/misc/cpp
        ${IMGUI_DIR}/backends
)

target_link_libraries(imgui_raylib
        PUBLIC
        raylib
)

# Editor as a library so a consuming project (the game) can link it into its own
# executable and register a game runtime for play-in-editor.
add_library(sage_editor STATIC ${EDITOR_SOURCES} ${EDITOR_HEADERS})

target_include_directories(sage_editor
        PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(sage_editor
        PUBLIC
        engine
        imgui_raylib
)

# Standalone editor: no game runtime registered, so Play mode is inert.
add_executable(editor main.cpp)

target_link_libraries(editor
        PRIVATE
        sage_editor
)

# Symbolic link for resources folder
set(source "${CMAKE_SOURCE_DIR}/resources")
set(destination "${CMAKE_CURRENT_BINARY_DIR}/resources")
add_custom_command(
        TARGET editor POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink ${source} ${destination}
        COMMENT "symbolic link resources folder from ${source} => ${destination}"
)
