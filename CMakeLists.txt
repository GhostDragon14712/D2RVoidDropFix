cmake_minimum_required(VERSION 3.29)
project(D2RVoidDropFix VERSION 1.0.0 LANGUAGES CXX RC)

include(FetchContent)

# Disable SDK examples and tests to keep build lean and fast
set(D2RLPLUGIN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    PluginSDK
    URL https://github.com/D2RLoader/PluginSDK/archive/refs/heads/master.zip
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(PluginSDK)

# Plugin DLL Target
add_library(D2RVoidDropFix SHARED
    void_drop_fix.cpp
    void_drop_fix.rc
)

target_include_directories(D2RVoidDropFix PRIVATE "${pluginsdk_SOURCE_DIR}/include")
target_link_libraries(D2RVoidDropFix PRIVATE D2RLPlugin::D2RLPlugin)
target_compile_features(D2RVoidDropFix PRIVATE cxx_std_20)

set_target_properties(D2RVoidDropFix PROPERTIES
    OUTPUT_NAME "d2rl-voiddropfix"
)

d2rlplugin_embed_config(
    TARGET D2RVoidDropFix
    FILE "${CMAKE_CURRENT_SOURCE_DIR}/void-drop-fix.toml"
)
