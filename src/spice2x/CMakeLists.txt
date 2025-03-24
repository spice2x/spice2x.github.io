cmake_minimum_required(VERSION 3.12)
cmake_policy(SET CMP0069 NEW)
project(spicetools)
include(CheckIPOSupported)

# set language level
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

# niceities for vscode
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# for RapidJSON
add_compile_definitions(RAPIDJSON_HAS_STDSTRING)

if(MSVC)
    # disable intermediate manifest
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /manifest:no")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /manifest:no")

    # disable warnings about using non _s variants like strncpy
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

    # disable warnings about using deprecated winsock2 functions
    add_compile_definitions(_WINSOCK_DEPRECATED_NO_WARNINGS)

    # RapidJSON does this
    add_compile_definitions(_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING)

    # define M_PI
    add_compile_definitions(_USE_MATH_DEFINES)

    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} /DYNAMICBASE:NO")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} /DYNAMICBASE:NO")

    set(USE_STATIC_MSVCRT ON CACHE BOOL "If enabled, will force the use of static crt instead of dynamic")

    if(USE_STATIC_MSVCRT)
        # use statically linked runtime
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
        set(CompilerFlags
                CMAKE_CXX_FLAGS
                CMAKE_CXX_FLAGS_DEBUG
                CMAKE_CXX_FLAGS_RELEASE
                CMAKE_C_FLAGS
                CMAKE_C_FLAGS_DEBUG
                CMAKE_C_FLAGS_RELEASE
        )

        foreach(CompilerFlag ${CompilerFlags})
            string(REPLACE "/MD" "/MT" ${CompilerFlag} "${${CompilerFlag}}")
        endforeach()
    endif()

    # disable C4996 "The POSIX name for this item is deprecated. Instead, use the ISO C and C++ conformant name" warning
    add_compile_options("/wd4996")

    # cleanup windows.h includes
    add_compile_options("/DNOMINMAX")

    # cleanup weird types
    add_compile_options("/DWINBOOL=BOOL")

    # add support for the deprecated std::result_of in c++20
    add_compile_options("/D_HAS_DEPRECATED_RESULT_OF")

    # enable build paralellization
    add_compile_options("/MP")

    # enable edit and continue in Debug
    add_compile_options("$<$<CONFIG:DEBUG>:/ZI>")
    add_link_options("$<$<CONFIG:DEBUG>:/SAFESEH:NO>")

    # enable fast pdb generation in debug
    add_link_options("$<$<CONFIG:DEBUG>:/DEBUG:FASTLINK>")

    # enable pdb generation for release builds
    add_compile_options("$<$<CONFIG:RELEASE,MINSIZEREL>:/Zi>")
    add_link_options("$<$<CONFIG:RELEASE,MINSIZEREL>:/DEBUG:FULL>")

    # enable COMDAT folding for even smaller release builds
    add_link_options("$<$<CONFIG:RELEASE,MINSIZEREL>:/OPT:ICF>")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    # disable warnings about using non _s variants like strncpy
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

    # disable warnings about using deprecated winsock2 functions
    add_compile_definitions(_WINSOCK_DEPRECATED_NO_WARNINGS)

    # RapidJSON does this
    add_compile_definitions(_SILENCE_CXX17_ITERATOR_BASE_CLASS_DEPRECATION_WARNING)

    # warnings
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wunknown-warning-option")

    # static linking
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static")
else()

    # warnings
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra") # enable stuff
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-pointer-arith") # but we love pointer arithmetic :)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unknown-pragmas") # since CLion does clang pragmas
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-address") # to allow checking function pointers for null
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-cast-function-type") # we actually do this a lot
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-class-memaccess") # RapidJSON does this
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-deprecated-declarations") # RapidJSON issue
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter") # for all those stubs
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-but-set-parameter") # for all those stubs
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-stringop-truncation") # since we do that from time to time
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-local-typedefs") # for our logging system
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-attributes") # fmtlib workaround

    # release flags
    if(CMAKE_BUILD_TYPE MATCHES "Release")

        # hide ident strings
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -fno-ident -ffunction-sections -fdata-sections")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fno-ident -ffunction-sections -fdata-sections")

        # a change in the linker caused the executable to be loaded above 4GB base virtual address
        # https://github.com/msys2/MINGW-packages/pull/6880
        # some games crash if some DLLS load above 4GB VA, so manually set base address to standard 32-bit VA,
        # and might as well double make sure ASLR is disabled here
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections,--disable-dynamicbase,--image-base=0x400000")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--gc-sections,--disable-dynamicbase,--image-base=0x400000")

        # set visibility to hidden
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -fvisibility=hidden")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fvisibility=hidden -fvisibility-inlines-hidden")

        # remove symbol table and relocation information
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -s")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -s")

        # performance
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -O2 -pipe")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2 -pipe")

        # ensure frame pointers are enabled
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -fno-omit-frame-pointer")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -fno-omit-frame-pointer")

        # no debug
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -DNDEBUG")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -DNDEBUG")

        # file prefix map for relative working directory
        set(CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE} -ffile-prefix-map=${CMAKE_SOURCE_DIR}=.")
        set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -ffile-prefix-map=${CMAKE_SOURCE_DIR}=.")
    endif()

    # release with debug info flags
    if(CMAKE_BUILD_TYPE MATCHES "RelWithDebInfo")

        # hide ident strings
        set(CMAKE_C_FLAGS_RELWITHDEBINFO "-fno-ident -ffunction-sections -fdata-sections")
        set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-fno-ident -ffunction-sections -fdata-sections")

        # linker fix to load below 4GB
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--gc-sections,--disable-dynamicbase,--image-base=0x400000")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--gc-sections,--disable-dynamicbase,--image-base=0x400000")

        # set visibility to hidden
        set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} -fvisibility=hidden")
        set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -fvisibility=hidden -fvisibility-inlines-hidden")

        # generate dwarf
        set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} -gdwarf")
        set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -gdwarf")

        # ensure frame pointers are enabled
        set(CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO} -fno-omit-frame-pointer")
        set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO} -fno-omit-frame-pointer")
    endif()

    # debug flags
    if(CMAKE_BUILD_TYPE MATCHES "Debug")

        # generate dwarf
        set(CMAKE_C_FLAGS_DEBUG "-gdwarf")
        set(CMAKE_CXX_FLAGS_DEBUG "-gdwarf")

        # linker fix to load below 4GB
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,--disable-dynamicbase,--image-base=0x400000")
        set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,--disable-dynamicbase,--image-base=0x400000")

        # enable debug symbols on level 3 and keep frame pointers
        set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -g3 -fno-omit-frame-pointer")
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g3 -fno-omit-frame-pointer")

        # optimize for debugging
        set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Og -pipe")
        set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Og -pipe")
    endif()

    # static linking
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -static -static-libgcc")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -static -static-libgcc -static-libstdc++")
endif()

# default defines
add_compile_definitions(
        WIN32_LEAN_AND_MEAN
        _WIN32_IE=0x0400
)

# acioemu log
#set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DACIOEMU_LOG")

# add project directory to include path so we can comfortably import
include_directories(${spicetools_SOURCE_DIR})

# add external libraries
add_subdirectory(external/fmt EXCLUDE_FROM_ALL)
add_subdirectory(external/discord-rpc EXCLUDE_FROM_ALL)
add_subdirectory(external/hash-library EXCLUDE_FROM_ALL)
add_compile_definitions(IMGUI_DISABLE_DEMO_WINDOWS)
add_compile_definitions(IMGUI_DISABLE_DEBUG_TOOLS)
add_subdirectory(external/imgui EXCLUDE_FROM_ALL)
add_subdirectory(external/minhook EXCLUDE_FROM_ALL)
add_subdirectory(external/cpu_features EXCLUDE_FROM_ALL)

# set link time optimizations (disabled for Debug builds for speed, disabled
# for RelWithDebInfo builds due to "lto1: error: two or more sections for"
# errors)
check_ipo_supported()
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_DEBUG OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO OFF)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_MINSIZEREL ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION_RELEASE ON)

# resources
###########
set_source_files_properties(build/manifest.rc PROPERTIES LANGUAGE RC)
set_source_files_properties(build/icon.rc PROPERTIES LANGUAGE RC)
set_source_files_properties(cfg/manifest.rc PROPERTIES LANGUAGE RC)
set_source_files_properties(cfg/icon.rc PROPERTIES LANGUAGE RC)
set_source_files_properties(cfg/Win32D.rc PROPERTIES LANGUAGE RC)
set_source_files_properties(build/manifest64.rc PROPERTIES LANGUAGE RC)

# sources
#########

set(SOURCE_FILES ${SOURCE_FILES}

        # acio
        acio/acio.cpp
        acio/module.cpp
        acio/pix/pix.cpp
        acio/core/core.cpp
        acio/hgth/hgth.cpp
        acio/bmpu/bmpu.cpp
        acio/hbhi/hbhi.cpp
        acio/hdxs/hdxs.cpp
        acio/kfca/kfca.cpp
        acio/i36g/i36g.cpp
        acio/panb/panb.cpp
        acio/icca/icca.cpp
        acio/j32d/j32d.cpp
        acio/bi2a/bi2a.cpp
        acio/klpa/klpa.cpp
        acio/mdxf/mdxf.cpp
        acio/pjei/pjei.cpp
        acio/pjec/pjec.cpp
        acio/i36i/i36i.cpp
        acio/nddb/nddb.cpp
        acio/la9a/la9a.cpp

        # acioemu
        acioemu/acioemu.cpp
        acioemu/device.cpp
        acioemu/handle.cpp
        acioemu/icca.cpp

        # acio2emu
        acio2emu/handle.cpp
        acio2emu/packet.cpp
        acio2emu/firmware/bi2x.cpp

        # api
        api/controller.cpp
        api/websocket.cpp
        api/request.cpp
        api/response.cpp
        api/module.cpp
        api/modules/card.cpp
        api/modules/buttons.cpp
        api/modules/capture.cpp
        api/modules/analogs.cpp
        api/modules/lights.cpp
        api/modules/memory.cpp
        api/modules/coin.cpp
        api/modules/info.cpp
        api/modules/keypads.cpp
        api/modules/control.cpp
        api/modules/touch.cpp
        api/modules/iidx.cpp
        api/serial.cpp
        api/modules/drs.cpp
        api/modules/lcd.cpp
        api/modules/ddr.cpp
        api/modules/resize.cpp

        # avs
        avs/core.cpp
        avs/ea3.cpp
        avs/game.cpp
        avs/automap.cpp
        avs/ssl.cpp

        # build
        build/defs.cpp

        # cfg
        cfg/spicecfg.cpp
        cfg/analog.cpp
        cfg/game.cpp
        cfg/button.cpp
        cfg/config.cpp
        cfg/api.cpp
        cfg/option.cpp
        cfg/light.cpp
        cfg/configurator.cpp
        cfg/configurator_wnd.cpp
        cfg/screen_resize.cpp

        # easrv
        easrv/easrv.cpp
        easrv/smartea.cpp

        # external asio
        external/asio/asiolist.cpp

        # external cardio
        external/cardio/cardio_hid.cpp
        external/cardio/cardio_window.cpp
        external/cardio/cardio_runner.cpp

        # external misc
        external/scard/scard.cpp
        external/stackwalker/stackwalker.cpp
        external/tinyxml2/tinyxml2.cpp
        external/http-parser/http_parser.c
        external/usbhidusage/usb-hid-usage.c
        external/toojpeg/toojpeg.cpp

        # games
        games/game.cpp
        games/io.cpp
        games/shared/lcdhandle.cpp
        games/shared/printer.cpp
        games/shared/twtouch.cpp
        games/popn/popn.cpp
        games/popn/io.cpp
        games/bbc/bbc.cpp
        games/bbc/io.cpp
        games/hpm/hpm.cpp
        games/hpm/io.cpp
        games/iidx/iidx.cpp
        games/iidx/io.cpp
        games/iidx/poke.cpp
        games/iidx/bi2a.cpp
        games/iidx/bi2x.cpp
        games/iidx/bi2x_hook.cpp
        games/iidx/ezusb.cpp
        games/iidx/legacy_camera.cpp
        games/iidx/local_camera.cpp
        games/iidx/camera.cpp
        games/iidx/mf_wrappers.cpp
        games/sdvx/bi2x_hook.cpp
        games/sdvx/sdvx.cpp
        games/sdvx/io.cpp
        games/sdvx/camera.cpp
        games/jb/jb.cpp
        games/jb/io.cpp
        games/nost/nost.cpp
        games/nost/io.cpp
        games/nost/poke.cpp
        games/gitadora/gitadora.cpp
        games/gitadora/io.cpp
        games/mga/mga.cpp
        games/mga/io.cpp
        games/mga/gunio.cpp
        games/sc/sc.cpp
        games/sc/io.cpp
        games/rb/rb.cpp
        games/rb/io.cpp
        games/rb/touch.cpp
        games/bs/bs.cpp
        games/bs/io.cpp
        games/rf3d/rf3d.cpp
        games/rf3d/io.cpp
        games/museca/io.cpp
        games/museca/museca.cpp
        games/dea/dea.cpp
        games/dea/io.cpp
        games/qma/qma.cpp
        games/qma/io.cpp games/qma/ezusb.cpp
        games/ddr/ddr.cpp
        games/ddr/io.cpp
        games/ddr/p3io/foot.cpp
        games/ddr/p3io/p3io.cpp
        games/ddr/p3io/sate.cpp
        games/ddr/p3io/usbmem.cpp
        games/ddr/p4io/p4io.cpp
        games/ddr/p4io/p4io.h
        games/mfc/mfc.cpp
        games/mfc/io.cpp
        games/ftt/ftt.cpp
        games/ftt/io.cpp
        games/loveplus/loveplus.cpp
        games/loveplus/io.cpp
        games/scotto/scotto.cpp
        games/scotto/io.cpp
        games/drs/drs.cpp
        games/drs/io.cpp
        games/we/we.cpp
        games/we/io.cpp
        games/we/touchpanel.cpp
        games/shogikai/shogikai.cpp
        games/shogikai/io.cpp
        games/otoca/otoca.cpp
        games/otoca/io.cpp
        games/otoca/p4io.cpp
        games/silentscope/silentscope.cpp
        games/silentscope/io.cpp
        games/pcm/pcm.cpp
        games/pcm/io.cpp
        games/onpara/onpara.cpp
        games/onpara/io.cpp
        games/onpara/westboard.cpp
        games/onpara/touchpanel.cpp
        games/bc/bc.cpp
        games/bc/io.cpp
        games/ccj/ccj.cpp
        games/ccj/io.cpp
        games/ccj/bi2x_hook.cpp
        games/ccj/trackball.cpp
        games/qks/qks.cpp
        games/qks/io.cpp
        games/qks/bi2x_hook.cpp
        games/mfg/mfg.cpp
        games/mfg/io.cpp
        games/mfg/bi2a_hook.cpp

        # hooks
        hooks/audio/audio.cpp
        hooks/audio/buffer.cpp
        hooks/audio/util.cpp
        hooks/audio/backends/dsound/dsound_backend.cpp
        hooks/audio/backends/mmdevice/audio_endpoint_volume.cpp
        hooks/audio/backends/mmdevice/device.cpp
        hooks/audio/backends/mmdevice/device_enumerator.cpp
        hooks/audio/backends/wasapi/audio_client.cpp
        hooks/audio/backends/wasapi/audio_render_client.cpp
        hooks/audio/backends/wasapi/dummy_audio_client.cpp
        hooks/audio/backends/wasapi/dummy_audio_clock.cpp
        hooks/audio/backends/wasapi/dummy_audio_render_client.cpp
        hooks/audio/backends/wasapi/dummy_audio_session_control.cpp
        hooks/audio/backends/wasapi/low_latency_client.cpp
        hooks/audio/backends/wasapi/util.cpp
        hooks/audio/implementations/asio.cpp
        hooks/audio/implementations/wave_out.cpp
        hooks/avshook.cpp
        hooks/cfgmgr32hook.cpp
        hooks/debughook.cpp
        hooks/devicehook.cpp
        hooks/graphics/graphics.cpp
        hooks/graphics/graphics_windowed.cpp
        hooks/graphics/nvapi_hook.cpp
        hooks/graphics/nvenc_hook.cpp
        hooks/graphics/backends/d3d9/d3d9_backend.cpp
        hooks/graphics/backends/d3d9/d3d9_device.cpp
        hooks/graphics/backends/d3d9/d3d9_fake_swapchain.cpp
        hooks/graphics/backends/d3d9/d3d9_swapchain.cpp
        hooks/graphics/backends/d3d9/d3d9_texture.cpp
        hooks/input/dinput8/fake_backend.cpp
        hooks/input/dinput8/fake_device.cpp
        hooks/input/dinput8/hook.cpp
        hooks/lang.cpp
        hooks/libraryhook.cpp
        hooks/networkhook.cpp
        hooks/powrprof.cpp
        #hooks/rom.cpp
        hooks/setupapihook.cpp
        hooks/sleephook.cpp
        hooks/unisintrhook.cpp
        hooks/winuser.cpp

        # launcher
        launcher/launcher.cpp
        launcher/signal.cpp
        launcher/superexit.cpp
        launcher/logger.cpp
        launcher/richpresence.cpp
        launcher/shutdown.cpp
        launcher/options.cpp

        # misc
        misc/bt5api.cpp
        misc/clipboard.cpp
        misc/device.cpp
        misc/eamuse.cpp
        misc/extdev.cpp
        misc/sciunit.cpp
        misc/sde.cpp
        misc/wintouchemu.cpp

        # nvapi
        nvapi/nvapi.cpp

        # overlay
        overlay/overlay.cpp
        overlay/window.cpp
        overlay/imgui/extensions.cpp
        overlay/imgui/impl_dx9.cpp
        overlay/imgui/impl_spice.cpp
        overlay/imgui/impl_sw.cpp
        overlay/windows/acio_status_buffers.cpp
        overlay/windows/camera_control.cpp
        overlay/windows/card_manager.cpp
        overlay/windows/drs_dancefloor.cpp
        overlay/windows/screen_resize.cpp
        overlay/windows/sdvx_sub.cpp
        overlay/windows/config.cpp
        overlay/windows/control.cpp
        overlay/windows/eadev.cpp
        overlay/windows/fps.cpp
        overlay/windows/generic_sub.cpp
        overlay/windows/iidx_seg.cpp
        overlay/windows/iidx_sub.cpp
        overlay/windows/iopanel.cpp
        overlay/windows/iopanel_ddr.cpp
        overlay/windows/iopanel_gfdm.cpp
        overlay/windows/iopanel_iidx.cpp
        overlay/windows/keypad.cpp
        overlay/windows/log.cpp
        overlay/windows/midi.cpp
        overlay/windows/patch_manager.cpp
        overlay/windows/wnd_manager.cpp

        # rawinput
        rawinput/rawinput.cpp
        rawinput/sextet.cpp
        rawinput/piuio.cpp
        rawinput/touch.cpp
        rawinput/hotplug.cpp
        rawinput/smx.cpp
        rawinput/smx.h
        rawinput/smxstage.cpp
        rawinput/smxstage.h
        rawinput/smxdedicab.cpp
        rawinput/smxdedicab.h

        # reader
        reader/reader.cpp
        reader/message.cpp
        reader/structuredmessage.cpp
        reader/crypt.cpp

        # stubs
        stubs/stubs.cpp

        # touch
        touch/touch.cpp
        touch/touch_indicators.cpp
        touch/win7.cpp
        touch/win8.cpp

        # util
        util/sigscan.cpp
        util/detour.cpp
        util/logging.cpp
        util/detour.cpp
        util/peb.cpp
        util/libutils.cpp
        util/fileutils.cpp
        util/resutils.cpp
        util/unity_player.cpp
        util/utils.cpp
        util/memutils.cpp
        util/rc4.cpp
        util/crypt.cpp
        util/time.cpp
        util/cpuutils.cpp
        util/netutils.cpp
        util/sysutils.cpp
        util/lz77.cpp
        util/tapeled.cpp
        util/execexe.cpp
)

source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} PREFIX "Source Files" FILES ${SOURCE_FILES})

# spice.exe
###########

set(RESOURCE_FILES build/manifest.manifest build/manifest.rc build/icon.rc cfg/Win32D.rc)
add_executable(spicetools_spice ${SOURCE_FILES} ${RESOURCE_FILES})
target_link_libraries(spicetools_spice
        PUBLIC d3d9 ws2_32 version comctl32 shlwapi iphlpapi hid secur32 setupapi psapi winmm winscard winhttp
        PRIVATE fmt-header-only discord-rpc imgui hash-library minhook imm32 dwmapi CpuFeatures::cpu_features)
set_target_properties(spicetools_spice PROPERTIES PREFIX "")
set_target_properties(spicetools_spice PROPERTIES OUTPUT_NAME "spice")

IF(NOT MSVC)
    set_target_properties(spicetools_spice PROPERTIES COMPILE_FLAGS "-m32" LINK_FLAGS "-m32")
endif()

# spice64.exe
#############

set(RESOURCE_FILES build/manifest.manifest build/manifest64.rc build/icon.rc cfg/Win32D.rc)
add_executable(spicetools_spice64 ${SOURCE_FILES} ${RESOURCE_FILES})

# do NOT link against: mf, mfplat, mfreadwrite; otherwise unity games will break
target_link_libraries(spicetools_spice64
        PUBLIC d3d9 ws2_32 version comctl32 shlwapi iphlpapi hid secur32 setupapi psapi winmm winscard winhttp mfuuid strmiids dxva2
        PRIVATE fmt-header-only discord-rpc imgui hash-library minhook imm32 dwmapi CpuFeatures::cpu_features)

set_target_properties(spicetools_spice64 PROPERTIES PREFIX "")
set_target_properties(spicetools_spice64 PROPERTIES OUTPUT_NAME "spice64")
target_compile_definitions(spicetools_spice64 PRIVATE SPICE64=1)

IF(NOT MSVC)
    set_target_properties(spicetools_spice64 PROPERTIES COMPILE_FLAGS "-m64" LINK_FLAGS "-m64")
endif()


# spicecfg.exe
##############

set(SOURCE_FILES ${SOURCE_FILES} launcher/options.h launcher/options.cpp)
set(RESOURCE_FILES cfg/manifest.manifest cfg/manifest.rc cfg/icon.rc cfg/Win32D.rc)
add_executable(spicetools_cfg WIN32 ${SOURCE_FILES} ${RESOURCE_FILES})
target_link_libraries(spicetools_cfg
        PUBLIC ws2_32 version comctl32 shlwapi iphlpapi hid secur32 setupapi psapi winmm winscard winhttp strmiids
        PRIVATE fmt-header-only discord-rpc imgui hash-library minhook imm32 dwmapi CpuFeatures::cpu_features)
set_target_properties(spicetools_cfg PROPERTIES PREFIX "")
set_target_properties(spicetools_cfg PROPERTIES OUTPUT_NAME "spicecfg")
target_compile_definitions(spicetools_cfg PRIVATE SPICETOOLS_SPICECFG_STANDALONE=1)

if(NOT MSVC)
    set_target_properties(spicetools_cfg PROPERTIES COMPILE_FLAGS "-m32" LINK_FLAGS "-m32")
endif()

# stubs
#######

# kbt.dll
set(SOURCE_FILES stubs/stubs.cpp)
add_library(spicetools_stubs_kbt SHARED ${SOURCE_FILES} stubs/stubs.def)
target_link_libraries(spicetools_stubs_kbt PRIVATE fmt-header-only)
set_target_properties(spicetools_stubs_kbt PROPERTIES PREFIX "")
set_target_properties(spicetools_stubs_kbt PROPERTIES OUTPUT_NAME "kbt")
target_compile_definitions(spicetools_stubs_kbt PRIVATE STUB=1)

if(NOT MSVC)
    set_target_properties(spicetools_stubs_kbt PROPERTIES COMPILE_FLAGS "-m32" LINK_FLAGS "-m32")
endif()

# kbt.dll 64bit
add_library(spicetools_stubs_kbt64 SHARED ${SOURCE_FILES} stubs/stubs.def)
target_link_libraries(spicetools_stubs_kbt64 PRIVATE fmt-header-only)
set_target_properties(spicetools_stubs_kbt64 PROPERTIES PREFIX "")
set_target_properties(spicetools_stubs_kbt64 PROPERTIES OUTPUT_NAME "kbt")
target_compile_definitions(spicetools_stubs_kbt64 PRIVATE STUB=1)

if(NOT MSVC)
    set_target_properties(spicetools_stubs_kbt64 PROPERTIES COMPILE_FLAGS "-m64" LINK_FLAGS "-m64")
endif()

# kld.dll
set(SOURCE_FILES stubs/stubs.cpp)
add_library(spicetools_stubs_kld SHARED ${SOURCE_FILES} stubs/stubs.def)
target_link_libraries(spicetools_stubs_kld PRIVATE fmt-header-only)
set_target_properties(spicetools_stubs_kld PROPERTIES PREFIX "")
set_target_properties(spicetools_stubs_kld PROPERTIES OUTPUT_NAME "kld")
target_compile_definitions(spicetools_stubs_kld PRIVATE STUB=1)

if(NOT MSVC)
    set_target_properties(spicetools_stubs_kld PROPERTIES COMPILE_FLAGS "-m32" LINK_FLAGS "-m32")
endif()

# kld.dll 64bit
add_library(spicetools_stubs_kld64 SHARED ${SOURCE_FILES} stubs/stubs.def)
target_link_libraries(spicetools_stubs_kld64 PRIVATE fmt-header-only)
set_target_properties(spicetools_stubs_kld64 PROPERTIES PREFIX "")
set_target_properties(spicetools_stubs_kld64 PROPERTIES OUTPUT_NAME "kld")
target_compile_definitions(spicetools_stubs_kld64 PRIVATE STUB=1)

if(NOT MSVC)
    set_target_properties(spicetools_stubs_kld64 PROPERTIES COMPILE_FLAGS "-m64" LINK_FLAGS "-m64")
endif()

# nvcuda.dll
set(SOURCE_FILES stubs/nvcuda.cpp)
add_library(spicetools_stubs_nvcuda SHARED ${SOURCE_FILES} stubs/nvcuda.def)
set_target_properties(spicetools_stubs_nvcuda PROPERTIES PREFIX "")
set_target_properties(spicetools_stubs_nvcuda PROPERTIES OUTPUT_NAME "nvcuda")

if(NOT MSVC)
    set_target_properties(spicetools_stubs_nvcuda PROPERTIES COMPILE_FLAGS "-m64" LINK_FLAGS "-m64")
endif()

# nvcuvid.dll
set(SOURCE_FILES stubs/nvcuvid.cpp)
add_library(spicetools_stubs_nvcuvid SHARED ${SOURCE_FILES} stubs/nvcuvid.def)
set_target_properties(spicetools_stubs_nvcuvid PROPERTIES PREFIX "")
set_target_properties(spicetools_stubs_nvcuvid PROPERTIES OUTPUT_NAME "nvcuvid")

if(NOT MSVC)
    set_target_properties(spicetools_stubs_nvcuvid PROPERTIES COMPILE_FLAGS "-m64" LINK_FLAGS "-m64")
endif()

# nvEncodeAPI64.dll
set(SOURCE_FILES stubs/nvEncodeAPI64.cpp)
add_library(spicetools_stubs_nvEncodeAPI64 SHARED ${SOURCE_FILES} stubs/nvEncodeAPI64.def)
set_target_properties(spicetools_stubs_nvEncodeAPI64 PROPERTIES PREFIX "")
set_target_properties(spicetools_stubs_nvEncodeAPI64 PROPERTIES OUTPUT_NAME "nvEncodeAPI64")

if(NOT MSVC)
    set_target_properties(spicetools_stubs_nvEncodeAPI64 PROPERTIES COMPILE_FLAGS "-m64" LINK_FLAGS "-m64")
endif()

# output directories
####################

# output config
set_target_properties(spicetools_cfg
        PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/spicetools")

# output 32bit
set_target_properties(spicetools_spice spicetools_stubs_kbt spicetools_stubs_kld
        PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/archive32"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/spicetools/32"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/spicetools/32")

# output 64bit
set_target_properties(spicetools_spice64 spicetools_stubs_kbt64 spicetools_stubs_kld64 spicetools_stubs_nvcuda spicetools_stubs_nvcuvid spicetools_stubs_nvEncodeAPI64
        PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/archive64"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/spicetools/64"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/spicetools/64")
