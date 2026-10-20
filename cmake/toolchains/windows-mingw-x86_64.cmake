set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

# Deliberately NOT setting CMAKE_TRY_COMPILE_TARGET_TYPE to STATIC_LIBRARY.
# That is the usual cross-compilation reflex, but it makes try_compile stop
# after the compile step, so every check_symbol_exists()/check_function_exists()
# that a dependency runs reports success for anything merely *declared* in a
# header. mingw-w64 declares arc4random_buf but the MSVCRT runtime does not
# provide it, so expat picked it as its entropy source and only failed much
# later, when exiv2 tried to link libexpat.a. This toolchain can link Windows
# executables fine — it just cannot run them — and link-based checks need
# exactly that and no more.

if(NOT DEFINED ENV{HAZOR_TARGET_ROOT})
    message(FATAL_ERROR "HAZOR_TARGET_ROOT must identify the Windows x86_64 dependency prefix")
endif()
set(CMAKE_FIND_ROOT_PATH "$ENV{HAZOR_TARGET_ROOT}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
