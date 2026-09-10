cmake_minimum_required(VERSION 3.25)

project(SaturatorMixFX VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(SMTG_CREATE_PLUGIN_LINK 0 CACHE BOOL "Disable automatic VST3 install links" FORCE)

if(NOT DEFINED VST3_SDK_ROOT)
    if(DEFINED ENV{VST3_SDK_ROOT})
        set(VST3_SDK_ROOT "$ENV{VST3_SDK_ROOT}")
    else()
        message(FATAL_ERROR "Set VST3_SDK_ROOT to the Steinberg VST3 SDK directory.")
    endif()
endif()

# Tell the Steinberg CMake modules exactly where the SDK lives.
set(vst3sdk_SOURCE_DIR "${VST3_SDK_ROOT}")
add_subdirectory("${vst3sdk_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/vst3sdk")
smtg_enable_vst3_sdk()

smtg_add_vst3plugin(SaturatorMixFX
    source/pluginids.h
    source/processor.h
    source/processor.cpp
    source/controller.h
    source/controller.cpp
    source/editor.h
    source/editor.cpp
    source/factory.cpp
)

target_link_libraries(SaturatorMixFX PRIVATE sdk vstgui_support)

smtg_target_add_plugin_resources(SaturatorMixFX
    RESOURCES
        resource/SMX3.uidesc
        resource/SMX3_MainGUI.png
        resource/SMX3_Nameplate_Runtime.png
        resource/125A_Badge_Runtime.png
        resource/Drive_Runtime.png
        resource/Triode_OUT_Runtime.png
        resource/Triode_IN_Runtime.png
        resource/Pentode_OUT_Runtime.png
        resource/Pentode_IN_Runtime.png
        resource/Iron_OUT_Runtime.png
        resource/Iron_IN_Runtime.png
        resource/LED_Amber_OFF_Runtime.png
        resource/LED_Amber_ON_Runtime.png
        resource/TubeGlow_1.png
        resource/TubeGlow_2.png
        resource/TubeGlow_3.png
)

smtg_target_configure_version_file(SaturatorMixFX)

target_compile_definitions(SaturatorMixFX PRIVATE
    SATURATORMIXFX_VERSION="${PROJECT_VERSION}"
)
