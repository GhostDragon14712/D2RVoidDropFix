cmake_minimum_required(VERSION 3.29)
project(D2RVoidDropFix VERSION 1.0.1 LANGUAGES CXX RC)

include(FetchContent)

# ------------------------------------------------------------------------------
# 1. Check for a local copy of PluginSDK first (offline & fast)
# ------------------------------------------------------------------------------
set(D2RLOADER_PLUGIN_SDK_DIR "" CACHE PATH "Optional local path to D2RLoader Plugin SDK")

set(LOCAL_SDK_CANDIDATES
    "${D2RLOADER_PLUGIN_SDK_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/../PluginSDK-master"
    "${CMAKE_CURRENT_SOURCE_DIR}/../PluginSDK"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../PluginSDK-master"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../PluginSDK"
)

set(FOUND_LOCAL_SDK FALSE)
foreach(candidate IN LISTS LOCAL_SDK_CANDIDATES)
    if(candidate AND EXISTS "${candidate}/CMakeLists.txt")
        message(STATUS "Found local D2RLoader Plugin SDK: ${candidate}")
        # Disable building examples when including SDK as dependency
        set(D2RLPLUGIN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        add_subdirectory("${candidate}" "${CMAKE_BINARY_DIR}/D2RLoader-PluginSDK")
        set(FOUND_LOCAL_SDK TRUE)
        break()
    endif()
endforeach()

# ------------------------------------------------------------------------------
# 2. If not found locally, automatically download via FetchContent
# ------------------------------------------------------------------------------
if(NOT FOUND_LOCAL_SDK)
    message(STATUS "Local PluginSDK not found. Downloading D2RLoader Plugin SDK via FetchContent...")
    
    # Don't build SDK example plugins during dependency build
    set(D2RLPLUGIN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)

    FetchContent_Declare(
        D2RLoaderPluginSDK
        GIT_REPOSITORY https://github.com/Dimentio/PluginSDK.git
        GIT_TAG        master
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(D2RLoaderPluginSDK)
endif()

# ------------------------------------------------------------------------------
# 3. Target Definition
# ------------------------------------------------------------------------------
add_library(D2RVoidDropFix SHARED
    void_drop_fix.cpp
    void_drop_fix.rc
)

target_link_libraries(D2RVoidDropFix PRIVATE D2RLPlugin::D2RLPlugin)

target_compile_features(D2RVoidDropFix PRIVATE cxx_std_20)

if(MSVC)
    target_compile_options(D2RVoidDropFix PRIVATE /W4 /permissive- /EHsc)
endif()

set_target_properties(D2RVoidDropFix PROPERTIES
    OUTPUT_NAME "d2rl-void-drop-fix"
    PREFIX ""
    RUNTIME_OUTPUT_DIRECTORY_DEBUG   "${CMAKE_BINARY_DIR}/bin/debug/d2rloader/plugins"
    RUNTIME_OUTPUT_DIRECTORY_RELEASE "${CMAKE_BINARY_DIR}/bin/release/d2rloader/plugins"
)

# Optional helper from PluginSDK to embed the default TOML config into the binary
if(COMMAND d2rlplugin_embed_config)
    d2rlplugin_embed_config(
        TARGET D2RVoidDropFix
        FILE "${CMAKE_CURRENT_SOURCE_DIR}/void-drop-fix.toml"
    )
endif()
