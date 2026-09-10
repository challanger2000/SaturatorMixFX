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

if(WIN32)
    target_sources(SaturatorMixFX PRIVATE
        ${vst3sdk_SOURCE_DIR}/public.sdk/source/main/dllmain.cpp
    )
endif()

smtg_target_add_plugin_resources(SaturatorMixFX
    RESOURCES
        resource/SMX3.uidesc
        resource/SMX3_MainGUI.png
        resource/SMX3_Drive.png
        resource/SMX3_Drive_Ring_Blue.png
        resource/SMX3_Button_OFF.png
        resource/SMX3_Button_PRESSED.png
        resource/SMX3_Button_Ring_Blue.png
        resource/SMX3_Cell_OFF.png
        resource/SMX3_Cell_LOW.png
        resource/SMX3_Cell_MEDIUM.png
        resource/SMX3_Cell_HIGH.png
        resource/SMX3_Cell_PEAK.png
        resource/SMX3_Base_Underlight.png
)

smtg_target_configure_version_file(SaturatorMixFX)

target_compile_definitions(SaturatorMixFX PRIVATE
    SATURATORMIXFX_VERSION="${PROJECT_VERSION}"
)
