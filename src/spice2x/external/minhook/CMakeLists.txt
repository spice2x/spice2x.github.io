cmake_minimum_required(VERSION 3.9)
cmake_policy(SET CMP0069 NEW)
project(minhook)

# set language level
set(CMAKE_CXX_STANDARD 17)

# warnings (based off SpiceTools)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra") # enable stuff
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-pointer-arith") # but we love pointer arithmetic :)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unknown-pragmas") # since CLion does clang pragmas
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-address") # to allow checking function pointers for null
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-cast-function-type") # we actually do this a lot
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-class-memaccess") # RapidJSON does this
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-parameter") # for all those stubs
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-stringop-truncation") # since we do that from time to time
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-unused-local-typedefs") # for our logging system

# library definition
set(SOURCE_FILES src/buffer.c src/hook.c src/trampoline.c src/hde/hde32.c src/hde/hde64.c)
add_library(minhook STATIC ${SOURCE_FILES} dll_resources/MinHook.def)
