# fails the build if a PE binary statically imports a forbidden DLL.
#
# some DLLs must never end up in spice's static import table, for two reasons:
#
#  1. user-overridable DLLs (e.g. DXVK's d3d9.dll): users drop their own copy
#     into the modules directory to replace the system one. a static import
#     forces the loader to load the SYSTEM copy at process startup - before the
#     modules directory is added to the DLL search path and before the game DLL
#     loads - so the user-supplied override never takes effect (see issue #779).
#
#  2. DLLs that break games when present (e.g. Media Foundation: mf/mfplat/
#     mfreadwrite): a static import loads them eagerly and breaks Unity games.
#
# in both cases the DLL must instead be loaded dynamically (libutils::try_library
# / GetProcAddress / delay load) so it is only pulled in when actually needed.
#
# invoked via `cmake -P` from a POST_BUILD step. required -D variables:
#   OBJDUMP     - path to objdump (CMAKE_OBJDUMP)
#   TARGET_FILE - path to the PE binary to inspect
#   FORBIDDEN   - semicolon-separated list of lowercase DLL names to reject

if(NOT OBJDUMP OR NOT EXISTS "${OBJDUMP}")
    message(WARNING
        "check_no_static_dll_imports: objdump not found, skipping import check for ${TARGET_FILE}")
    return()
endif()

execute_process(
    COMMAND "${OBJDUMP}" -p "${TARGET_FILE}"
    OUTPUT_VARIABLE dump_output
    RESULT_VARIABLE dump_result
    ERROR_VARIABLE dump_error)

if(NOT dump_result EQUAL 0)
    message(WARNING
        "check_no_static_dll_imports: objdump failed for ${TARGET_FILE}: ${dump_error}")
    return()
endif()

# both GNU objdump and llvm-objdump print one "DLL Name: <name>" line per
# statically imported DLL in their PE private-header dump.
string(REGEX MATCHALL "DLL Name:[ \t]*[^\n\r]+" dll_lines "${dump_output}")

set(violations "")
foreach(line IN LISTS dll_lines)
    string(REGEX REPLACE "DLL Name:[ \t]*" "" dll_name "${line}")
    string(STRIP "${dll_name}" dll_name)
    string(TOLOWER "${dll_name}" dll_name_lower)
    if(dll_name_lower IN_LIST FORBIDDEN)
        list(APPEND violations "${dll_name}")
    endif()
endforeach()

if(violations)
    list(REMOVE_DUPLICATES violations)
    string(REPLACE ";" ", " violations_str "${violations}")
    message(FATAL_ERROR
        "static DLL import check FAILED for ${TARGET_FILE}\n"
        "  forbidden static imports found: ${violations_str}\n"
        "\n"
        "  these DLLs must never be statically imported by spice:\n"
        "    * user-overridable DLLs (e.g. DXVK d3d9.dll) - a static import loads the\n"
        "      system copy at startup and preempts the modules override (issue #779).\n"
        "    * Media Foundation DLLs (mf/mfplat/mfreadwrite) - a static import breaks\n"
        "      Unity games.\n"
        "\n"
        "  fix: load the DLL dynamically instead - replace the direct API call with a\n"
        "  libutils::try_library() + libutils::try_proc() lookup (or a delay load), then\n"
        "  call through the resolved function pointer.")
endif()

message(STATUS "static DLL import check passed for ${TARGET_FILE}")
