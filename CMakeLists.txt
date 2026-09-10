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

add_subdirectory("${VST3_SDK_ROOT}" "${CMAKE_BINARY_DIR}/vst3sdk")

smtg_add_vst3plugin(SaturatorMixFX
    source/pluginids.h
    source/processor.h
    source/processor.cpp
    source/controller.h
    source/controller.cpp
    source/factory.cpp
)

target_link_libraries(SaturatorMixFX PRIVATE sdk)

target_compile_definitions(SaturatorMixFX PRIVATE
    SATURATORMIXFX_VERSION="${PROJECT_VERSION}"
)

if(SMTG_WIN)
    target_sources(SaturatorMixFX PRIVATE resource/win32resource.rc)
endif()
