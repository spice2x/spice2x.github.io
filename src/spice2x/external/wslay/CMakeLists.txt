cmake_minimum_required(VERSION 3.12)
project(wslay C)

set(WSLAY_HEADERS
        msvc_compat.h
        wslay_event.h
        wslay_frame.h
        wslay_macro.h
        wslay_net.h
        wslay_queue.h
        includes/wslay/wslay.h
        includes/wslay/wslayver.h
)
set(WSLAY_SOURCES
        wslay_event.c
        wslay_frame.c
        wslay_net.c
        wslay_queue.c
)

add_library(wslay STATIC ${WSLAY_HEADERS} ${WSLAY_SOURCES})

# config.h here is checked in rather than generated, see the note in it
target_compile_definitions(wslay PRIVATE HAVE_CONFIG_H)
target_include_directories(wslay PRIVATE ${PROJECT_SOURCE_DIR})
target_include_directories(wslay PUBLIC ${PROJECT_SOURCE_DIR}/includes)

if(MSVC)
        target_compile_options(wslay PRIVATE "/FI${PROJECT_SOURCE_DIR}/msvc_compat.h")
else()
        # upstream code, and not ours to keep warning clean
        target_compile_options(wslay PRIVATE -w)
endif()
