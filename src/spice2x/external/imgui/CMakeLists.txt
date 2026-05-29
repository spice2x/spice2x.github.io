cmake_minimum_required(VERSION 3.12)
project(imgui CXX)

# dear imgui uses the C++ virtual interfaces for DirectX
remove_definitions(-DCINTERFACE)

set(IMGUI_HEADERS
        imconfig.h
        imgui.h
        imgui_internal.h
        backends/imgui_impl_dx9.h
        misc/cpp/imgui_stdlib.h
)
set(IMGUI_SOURCES
        imgui.cpp
        imgui_draw.cpp
        imgui_tables.cpp
        imgui_widgets.cpp
        imgui_demo.cpp
        backends/imgui_impl_dx9.cpp
        misc/cpp/imgui_stdlib.cpp
)

# spice2x: DX11 backend is only built for non-XP toolchains. WinXP lacks
# D3DCOMPILER_47.dll, and including this TU would pull in D3DCompile imports.
if(NOT SPICE_XP)
    list(APPEND IMGUI_HEADERS backends/imgui_impl_dx11.h)
    list(APPEND IMGUI_SOURCES backends/imgui_impl_dx11.cpp)
endif()

add_library(imgui STATIC ${IMGUI_HEADERS} ${IMGUI_SOURCES})
target_include_directories(imgui PRIVATE ${PROJECT_SOURCE_DIR})
