cmake_minimum_required(VERSION 3.26)
project(sage)

set(CMAKE_CXX_STANDARD 20)
include_directories(${SDL2_INCLUDE_DIRS} vendor vendor/imgui)
set(SOURCE_FILES
        main.cpp
        constants.hpp
        ObjParser.cpp
        ObjParser.hpp
        Mesh.hpp
        utils.hpp
        Renderer.cpp
        Renderer.hpp
        Renderable.hpp
        vendor/lodepng.cpp
        vendor/lodepng.h
        vendor/stb_image.h
        vendor/imgui/imgui.cpp
        vendor/imgui/imgui.h
        vendor/imgui/imgui_internal.h
        vendor/imgui/imgui_draw.cpp
        vendor/imgui/imgui_tables.cpp
        vendor/imgui/imgui_tables.cpp
        vendor/imgui/imgui_widgets.cpp
        vendor/imgui/imstb_rectpack.h
        vendor/imgui/imstb_textedit.h
        vendor/imgui/imstb_truetype.h
        vendor/imgui/backends/imgui_impl_sdlrenderer2.cpp
        vendor/imgui/backends/imgui_impl_sdlrenderer2.h
        vendor/imgui/backends/imgui_impl_sdl2.cpp
        vendor/imgui/backends/imgui_impl_sdl2.h
        vendor/imgui/imgui_demo.cpp
        Application.cpp
        Application.hpp
        GUI.cpp
        GUI.hpp
        Camera.cpp
        Camera.hpp
        Scene.cpp
        Scene.hpp
        slib.hpp
        SceneData.hpp
        Event.cpp
        Event.hpp
        EventManager.cpp
        EventManager.hpp
        EventCallback.hpp
        Rasterizer.cpp
        Rasterizer.hpp
        ZBuffer.hpp
        slib.cpp
        Mesh.cpp
        Model.cpp
        Model.hpp
        Model.cpp
)

include(FetchContent)
set(FETCHCONTENT_BASE_DIR ${PROJECT_SOURCE_DIR}/libs CACHE PATH "Missing description." FORCE)
FetchContent_Declare(assimp
        GIT_REPOSITORY https://github.com/assimp/assimp.git
        GIT_TAG master)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ASSIMP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(ASSIMP_INJECT_DEBUG_POSTFIX OFF CACHE BOOL "" FORCE)
set(ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(assimp)


set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -std=c++20 -O3")
add_executable(${PROJECT_NAME} ${SOURCE_FILES})
target_include_directories(${PROJECT_NAME} PRIVATE libs/assimp-src/include)

find_package(SDL2 REQUIRED)
find_package(glm REQUIRED)
target_link_libraries(${PROJECT_NAME} ${SDL2_LIBRARIES} glm::glm assimp)
find_package(OpenMP REQUIRED)
if(OpenMP_CXX_FOUND)
    target_link_libraries(${PROJECT_NAME} OpenMP::OpenMP_CXX)
endif()
set(source "${CMAKE_SOURCE_DIR}/resources")
set(destination "${CMAKE_CURRENT_BINARY_DIR}/resources")
add_custom_command(
        TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E create_symlink ${source} ${destination}
        DEPENDS ${destination}
        COMMENT "symbolic link resources folder from ${source} => ${destination}"
)




