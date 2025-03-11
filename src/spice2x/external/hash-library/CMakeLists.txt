cmake_minimum_required(VERSION 3.9)
project(imgui CXX)

set(HASH_LIBRARY_HEADERS
        crc32.h
        hash.h
        hmac.h
        keccak.h
        md5.h
        sha1.h
        sha3.h
        sha256.h
)
set(HASH_LIBRARY_SOURCES
        crc32.cpp
        digest.cpp
        keccak.cpp
        md5.cpp
        sha1.cpp
        sha3.cpp
        sha256.cpp
)

add_library(hash-library STATIC ${HASH_LIBRARY_HEADERS} ${HASH_LIBRARY_SOURCES})
target_include_directories(hash-library PRIVATE ${PROJECT_SOURCE_DIR})
