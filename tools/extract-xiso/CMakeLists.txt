cmake_minimum_required(VERSION 3.25)

project(MojoRecompExtractXiso LANGUAGES C)

set(EXTRACT_XISO_ROOT "" CACHE PATH "Pinned extract-xiso source checkout")
if(NOT EXTRACT_XISO_ROOT OR NOT EXISTS "${EXTRACT_XISO_ROOT}/extract-xiso.c")
    message(FATAL_ERROR "EXTRACT_XISO_ROOT must point at the pinned extract-xiso source checkout")
endif()

add_executable(extract-xiso
    "${EXTRACT_XISO_ROOT}/extract-xiso.c")

set_target_properties(extract-xiso PROPERTIES
    C_STANDARD 90
    C_STANDARD_REQUIRED YES
    C_EXTENSIONS YES)

target_compile_definitions(extract-xiso PRIVATE NDEBUG NO_FTP)

if(WIN32)
    # The Windows headers do not provide BYTE_ORDER or BIG_ENDIAN; glibc's
    # <sys/types.h> does, so they are only defined here.
    target_compile_definitions(extract-xiso PRIVATE
        WIN32
        _CONSOLE
        BYTE_ORDER=1234
        BIG_ENDIAN=4321
        _CRT_SECURE_NO_WARNINGS)
    target_link_libraries(extract-xiso PRIVATE ws2_32)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Upstream selects its POSIX large-file configuration with this macro.
    target_compile_definitions(extract-xiso PRIVATE __LINUX__)
else()
    message(FATAL_ERROR "The MojoRecomp extract-xiso build supports Windows and Linux only")
endif()

if(MSVC OR CMAKE_C_SIMULATE_ID STREQUAL "MSVC")
    target_compile_options(extract-xiso PRIVATE /O2)
    if(CMAKE_C_COMPILER_ID MATCHES "Clang")
        target_compile_options(extract-xiso PRIVATE
            /clang:-std=gnu89
            /clang:-Wno-deprecated-declarations
            /clang:-Wno-deprecated-non-prototype)
    endif()
elseif(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(extract-xiso PRIVATE
        -std=gnu89
        -Wno-deprecated-declarations
        $<$<C_COMPILER_ID:Clang>:-Wno-deprecated-non-prototype>)
endif()
