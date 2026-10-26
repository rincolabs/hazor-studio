set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(DEFINED ENV{CC} AND NOT "$ENV{CC}" STREQUAL "")
    set(CMAKE_C_COMPILER "$ENV{CC}")
endif()
if(DEFINED ENV{CXX} AND NOT "$ENV{CXX}" STREQUAL "")
    set(CMAKE_CXX_COMPILER "$ENV{CXX}")
endif()

if(DEFINED ENV{HAZOR_TARGET_ROOT})
    list(PREPEND CMAKE_FIND_ROOT_PATH "$ENV{HAZOR_TARGET_ROOT}")
endif()

# Programs execute on the Linux host. Target libraries are normally passed as
# explicit CMake package roots; system ABI libraries remain available from the
# controlled Linux environment (or the Flatpak SDK).
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
