if(NOT DEFINED ENV{OSXCROSS_TARGET_DIR})
    message(FATAL_ERROR "OSXCROSS_TARGET_DIR must point to a prepared OSXCross target")
endif()
if(NOT DEFINED ENV{HAZOR_TARGET_ARCH})
    message(FATAL_ERROR "HAZOR_TARGET_ARCH must be x86_64 or arm64")
endif()

set(CMAKE_OSX_ARCHITECTURES "$ENV{HAZOR_TARGET_ARCH}" CACHE STRING "" FORCE)
# Qt 6.8's supported macOS floor is 12 for both Intel and Apple Silicon.
# Lowering the application target below the target Qt build is not valid even
# when an older SDK can still link the executable.
set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "" FORCE)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
include("$ENV{OSXCROSS_TARGET_DIR}/toolchain.cmake")

if(DEFINED ENV{HAZOR_TARGET_ROOT})
    list(PREPEND CMAKE_FIND_ROOT_PATH "$ENV{HAZOR_TARGET_ROOT}")
endif()
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)
