cmake_minimum_required(VERSION 3.29)
project(D2RVoidDropFix VERSION 1.0.0 LANGUAGES CXX RC)

# Auto-detect PluginSDK location
if(NOT DEFINED D2RLOADER_PLUGIN_SDK_DIR OR NOT EXISTS "${D2RLOADER_PLUGIN_SDK_DIR}/CMakeLists.txt")
    set(POTENTIAL_SDK_DIRS
        "${CMAKE_CURRENT_SOURCE_DIR}/../PluginSDK-master"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../PluginSDK-master"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../../PluginSDK-master"
        "${CMAKE_CURRENT_SOURCE_DIR}/../PluginSDK"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../PluginSDK"
    )
    foreach(candidate IN LISTS POTENTIAL_SDK_DIRS)
        if(EXISTS "${candidate}/CMakeLists.txt")
            set(D2RLOADER_PLUGIN_SDK_DIR "${candidate}" CACHE PATH "Path to D2RLoader Plugin SDK" FORCE)
            break()
        endif()
    endforeach()
endif()

if(NOT EXISTS "${D2RLOADER_PLUGIN_SDK_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "Could not find PluginSDK-master. Pass -DD2RLOADER_PLUGIN_SDK_DIR=\"path/to/PluginSDK-master\" to cmake.")
endif()

add_subdirectory("${D2RLOADER_PLUGIN_SDK_DIR}" "${CMAKE_BINARY_DIR}/D2RLoader-PluginSDK")

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
)
