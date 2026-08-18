# Minimal build for the vendored libjpeg-turbo.
# Upstream's CMake pulls in Java bindings, tools, tests and packaging and sets
# its own global flags, none of which survive contact with this project's four
# cross toolchains, so only the static library is described here.

set(LJT_VERSION "3.0.4")
set(VERSION ${LJT_VERSION})
set(JPEG_LIB_VERSION 62)
set(LIBJPEG_TURBO_VERSION_NUMBER 3000004)
set(BUILD "spice2x")

set(C_ARITH_CODING_SUPPORTED 1)
set(D_ARITH_CODING_SUPPORTED 1)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(LJT_ARCH "x86_64")
else()
    set(LJT_ARCH "i386")
endif()

# SIMD is the entire reason for taking this dependency, but a missing assembler
# must not break the build; the C paths are still correct, just slower.
# ASM_NASM is enabled by the top level CMakeLists.
if(CMAKE_ASM_NASM_COMPILER)
    set(WITH_SIMD 1)
else()
    message(WARNING "nasm not found, building libjpeg-turbo without SIMD (screen capture will be slower)")
endif()

if(MSVC)
    set(HIDDEN "")
    set(INLINE "__forceinline")
    set(THREAD_LOCAL "__declspec(thread)")
    set(HAVE_INTRIN_H 1)
else()
    set(HIDDEN "__attribute__((visibility(\"hidden\")))")
    set(INLINE "inline __attribute__((always_inline))")
    set(THREAD_LOCAL "__thread")
    set(HAVE_BUILTIN_CTZL 1)
endif()

set(SIZE_T ${CMAKE_SIZEOF_VOID_P})

configure_file(jconfig.h.in ${CMAKE_CURRENT_BINARY_DIR}/jconfig.h)
configure_file(jconfigint.h.in ${CMAKE_CURRENT_BINARY_DIR}/jconfigint.h)
configure_file(jversion.h.in ${CMAKE_CURRENT_BINARY_DIR}/jversion.h)

# libjpeg-turbo 3.x supports 8, 12 and 16 bit samples by compiling the
# precision dependent units once per precision; jcinit.c dispatches to all
# three at runtime, so none of them can be dropped.
# jstdhuff.c and the *ext.c units are textually included and never compiled.
set(JPEG16_SOURCES
        jcapistd.c jccolor.c jcdiffct.c jclossls.c jcmainct.c jcprepct.c
        jcsample.c jdapistd.c jdcolor.c jddiffct.c jdlossls.c jdmainct.c
        jdpostct.c jdsample.c jutils.c)

set(JPEG12_SOURCES ${JPEG16_SOURCES}
        jccoefct.c jcdctmgr.c jdcoefct.c jddctmgr.c jdmerge.c jfdctfst.c
        jfdctint.c jidctflt.c jidctfst.c jidctint.c jidctred.c jquant1.c
        jquant2.c)

set(JPEG_SOURCES ${JPEG12_SOURCES}
        jcapimin.c jchuff.c jcicc.c jcinit.c jclhuff.c jcmarker.c jcmaster.c
        jcomapi.c jcparam.c jcphuff.c jctrans.c jdapimin.c jdatadst.c
        jdatasrc.c jdhuff.c jdicc.c jdinput.c jdlhuff.c jdmarker.c jdmaster.c
        jdphuff.c jdtrans.c jerror.c jfdctflt.c jmemmgr.c jmemnobs.c
        jpeg_nbits.c
        jaricom.c jcarith.c jdarith.c)

if(WITH_SIMD)
    # the *ext-*.asm units are textually included by these, so the list is
    # explicit rather than globbed
    if(LJT_ARCH STREQUAL "x86_64")
        set(LJT_SIMD_ASM
                x86_64/jsimdcpu.asm x86_64/jfdctflt-sse.asm
                x86_64/jccolor-sse2.asm x86_64/jcgray-sse2.asm x86_64/jchuff-sse2.asm
                x86_64/jcphuff-sse2.asm x86_64/jcsample-sse2.asm x86_64/jdcolor-sse2.asm
                x86_64/jdmerge-sse2.asm x86_64/jdsample-sse2.asm x86_64/jfdctfst-sse2.asm
                x86_64/jfdctint-sse2.asm x86_64/jidctflt-sse2.asm x86_64/jidctfst-sse2.asm
                x86_64/jidctint-sse2.asm x86_64/jidctred-sse2.asm x86_64/jquantf-sse2.asm
                x86_64/jquanti-sse2.asm
                x86_64/jccolor-avx2.asm x86_64/jcgray-avx2.asm x86_64/jcsample-avx2.asm
                x86_64/jdcolor-avx2.asm x86_64/jdmerge-avx2.asm x86_64/jdsample-avx2.asm
                x86_64/jfdctint-avx2.asm x86_64/jidctint-avx2.asm x86_64/jquanti-avx2.asm)
    else()
        set(LJT_SIMD_ASM
                i386/jsimdcpu.asm i386/jfdctflt-3dn.asm
                i386/jidctflt-3dn.asm i386/jquant-3dn.asm
                i386/jccolor-mmx.asm i386/jcgray-mmx.asm i386/jcsample-mmx.asm
                i386/jdcolor-mmx.asm i386/jdmerge-mmx.asm i386/jdsample-mmx.asm
                i386/jfdctfst-mmx.asm i386/jfdctint-mmx.asm i386/jidctfst-mmx.asm
                i386/jidctint-mmx.asm i386/jidctred-mmx.asm i386/jquant-mmx.asm
                i386/jfdctflt-sse.asm i386/jidctflt-sse.asm i386/jquant-sse.asm
                i386/jccolor-sse2.asm i386/jcgray-sse2.asm i386/jchuff-sse2.asm
                i386/jcphuff-sse2.asm i386/jcsample-sse2.asm i386/jdcolor-sse2.asm
                i386/jdmerge-sse2.asm i386/jdsample-sse2.asm i386/jfdctfst-sse2.asm
                i386/jfdctint-sse2.asm i386/jidctflt-sse2.asm i386/jidctfst-sse2.asm
                i386/jidctint-sse2.asm i386/jidctred-sse2.asm i386/jquantf-sse2.asm
                i386/jquanti-sse2.asm
                i386/jccolor-avx2.asm i386/jcgray-avx2.asm i386/jcsample-avx2.asm
                i386/jdcolor-avx2.asm i386/jdmerge-avx2.asm i386/jdsample-avx2.asm
                i386/jfdctint-avx2.asm i386/jidctint-avx2.asm i386/jquanti-avx2.asm)
    endif()

    list(TRANSFORM LJT_SIMD_ASM PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/simd/")
    list(APPEND JPEG_SOURCES simd/${LJT_ARCH}/jsimd.c ${LJT_SIMD_ASM})

    # nasm resolves %include against these, and requires the trailing separator
    set(CMAKE_ASM_NASM_FLAGS
            "${CMAKE_ASM_NASM_FLAGS} -I${CMAKE_CURRENT_SOURCE_DIR}/simd/nasm/ -I${CMAKE_CURRENT_SOURCE_DIR}/simd/${LJT_ARCH}/")

    if(LJT_ARCH STREQUAL "x86_64")
        set(CMAKE_ASM_NASM_OBJECT_FORMAT win64)
        set(CMAKE_ASM_NASM_FLAGS "${CMAKE_ASM_NASM_FLAGS} -D__x86_64__ -DWIN64")
    else()
        set(CMAKE_ASM_NASM_OBJECT_FORMAT win32)
        set(CMAKE_ASM_NASM_FLAGS "${CMAKE_ASM_NASM_FLAGS} -DWIN32")
    endif()
endif()

add_library(jpeg12-static OBJECT ${JPEG12_SOURCES})
set_property(TARGET jpeg12-static PROPERTY COMPILE_FLAGS "-DBITS_IN_JSAMPLE=12")

add_library(jpeg16-static OBJECT ${JPEG16_SOURCES})
set_property(TARGET jpeg16-static PROPERTY COMPILE_FLAGS "-DBITS_IN_JSAMPLE=16")

add_library(jpeg-static STATIC ${JPEG_SOURCES}
        $<TARGET_OBJECTS:jpeg12-static>
        $<TARGET_OBJECTS:jpeg16-static>)

foreach(ljt_target jpeg-static jpeg12-static jpeg16-static)
    target_include_directories(${ljt_target} PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}
            ${CMAKE_CURRENT_BINARY_DIR})

    target_include_directories(${ljt_target} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/simd
            ${CMAKE_CURRENT_SOURCE_DIR}/simd/${LJT_ARCH})

    if(NOT MSVC)
        target_compile_options(${ljt_target} PRIVATE -Wno-unused-parameter -Wno-sign-compare)
    endif()
endforeach()
