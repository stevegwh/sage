# CMakeLists.txt

if (NOT DEFINED SAGE_ROOT_DIR)
    get_filename_component(SAGE_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)
endif ()

# Path the consuming project provides for engine extension headers
# (e.g. project/CustomCollisionLayers.hpp). When unset, falls back to the
# engine's stub directory so the engine builds standalone.
if(NOT DEFINED SAGE_PROJECT_INCLUDE_DIR)
    set(SAGE_PROJECT_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/defaults)
endif()

file(GLOB_RECURSE CORE_HEADERS *.hpp)
file(GLOB_RECURSE CORE_SOURCES *.cpp)

# Exclude the standalone-build stub from the glob — it shadows the real
# project header at compile time only via the include path.
list(FILTER CORE_HEADERS EXCLUDE REGEX "/defaults/")

add_library(engine STATIC ${CORE_SOURCES} ${CORE_HEADERS})

# Sage owns the stable managed host and Microsoft's supported native hosting API.
find_program(SAGE_DOTNET_EXECUTABLE dotnet REQUIRED)
get_filename_component(SAGE_DOTNET_EXECUTABLE_REAL "${SAGE_DOTNET_EXECUTABLE}" REALPATH)
get_filename_component(SAGE_DOTNET_ROOT "${SAGE_DOTNET_EXECUTABLE_REAL}" DIRECTORY)

if (APPLE)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        set(SAGE_DOTNET_RID osx-arm64)
    else ()
        set(SAGE_DOTNET_RID osx-x64)
    endif ()
elseif (WIN32)
    if (CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(SAGE_DOTNET_RID win-x64)
    else ()
        set(SAGE_DOTNET_RID win-x86)
    endif ()
elseif (UNIX)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)$")
        set(SAGE_DOTNET_RID linux-arm64)
    else ()
        set(SAGE_DOTNET_RID linux-x64)
    endif ()
else ()
    message(FATAL_ERROR "Sage C# scripting does not know the .NET runtime identifier for this platform.")
endif ()

file(GLOB SAGE_DOTNET_NATIVE_HOST_DIRS
        LIST_DIRECTORIES true
        "${SAGE_DOTNET_ROOT}/packs/Microsoft.NETCore.App.Host.${SAGE_DOTNET_RID}/10.0.*/runtimes/${SAGE_DOTNET_RID}/native")
list(SORT SAGE_DOTNET_NATIVE_HOST_DIRS COMPARE NATURAL ORDER DESCENDING)
if (NOT SAGE_DOTNET_NATIVE_HOST_DIRS)
    message(FATAL_ERROR ".NET 10 native hosting pack was not found for ${SAGE_DOTNET_RID} under ${SAGE_DOTNET_ROOT}.")
endif ()
list(GET SAGE_DOTNET_NATIVE_HOST_DIRS 0 SAGE_DOTNET_NATIVE_HOST_DIR)
if (WIN32)
    find_library(SAGE_NETHOST_LIBRARY NAMES nethost PATHS "${SAGE_DOTNET_NATIVE_HOST_DIR}" NO_DEFAULT_PATH REQUIRED)
else ()
    find_file(SAGE_NETHOST_LIBRARY NAMES libnethost.a PATHS "${SAGE_DOTNET_NATIVE_HOST_DIR}" NO_DEFAULT_PATH REQUIRED)
endif ()

add_executable(sage_script_api_codegen "${SAGE_ROOT_DIR}/tools/GenerateScriptApi.cpp")
target_link_libraries(sage_script_api_codegen PRIVATE engine)

set(SAGE_GENERATED_SCRIPT_API
        "${SAGE_ROOT_DIR}/managed/Sage.Scripting/Generated/ScriptComponents.g.cs")
add_custom_command(
        OUTPUT "${SAGE_GENERATED_SCRIPT_API}"
        COMMAND "$<TARGET_FILE:sage_script_api_codegen>" "${SAGE_GENERATED_SCRIPT_API}"
        DEPENDS sage_script_api_codegen
        COMMENT "Generating the Sage C# component API"
        VERBATIM)

set(SAGE_MANAGED_PROJECT "${SAGE_ROOT_DIR}/managed/Sage.Scripting/Sage.Scripting.csproj")
set(SAGE_MANAGED_OUTPUT "${SAGE_ROOT_DIR}/managed/Sage.Scripting/bin/Debug/net10.0")
file(GLOB SAGE_MANAGED_SOURCES CONFIGURE_DEPENDS
        "${SAGE_ROOT_DIR}/managed/Sage.Scripting/*.cs"
        "${SAGE_ROOT_DIR}/managed/Sage.Scripting/*.csproj")
add_custom_command(
        OUTPUT
        "${SAGE_MANAGED_OUTPUT}/Sage.Scripting.dll"
        "${SAGE_MANAGED_OUTPUT}/Sage.Scripting.runtimeconfig.json"
        COMMAND "${SAGE_DOTNET_EXECUTABLE}" build "${SAGE_MANAGED_PROJECT}" --configuration Debug --nologo
        DEPENDS ${SAGE_MANAGED_SOURCES} "${SAGE_GENERATED_SCRIPT_API}"
        COMMENT "Building the Sage C# scripting host"
        VERBATIM)
add_custom_target(sage_managed_host ALL
        DEPENDS "${SAGE_MANAGED_OUTPUT}/Sage.Scripting.dll")

target_link_libraries(engine
        PUBLIC
        raylib
        EnTT::EnTT
        PRIVATE
        "${SAGE_NETHOST_LIBRARY}"
        ${CMAKE_DL_LIBS}
)

target_include_directories(engine
        PRIVATE
        # For internal engine files - they can use "components/..."
        ${CMAKE_CURRENT_SOURCE_DIR}
        PUBLIC
        # For external consumers (game) - they use "components/..."
        ${SAGE_ROOT_DIR}
        ${SAGE_ROOT_DIR}/vendor
        ${SAGE_ROOT_DIR}/vendor/magic_enum
        # Resolves project/CustomCollisionLayers.hpp (and any future project
        # extension headers). Defaults to engine/defaults/ for standalone builds.
        ${SAGE_PROJECT_INCLUDE_DIR}
)

target_include_directories(engine PRIVATE "${SAGE_DOTNET_NATIVE_HOST_DIR}")
target_compile_definitions(engine PRIVATE
        SAGE_MANAGED_RUNTIME_CONFIG_PATH="${SAGE_MANAGED_OUTPUT}/Sage.Scripting.runtimeconfig.json"
        SAGE_MANAGED_HOST_ASSEMBLY_PATH="${SAGE_MANAGED_OUTPUT}/Sage.Scripting.dll")
