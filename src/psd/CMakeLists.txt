# `psd` — static library: a native C++20 PSD/PSB reader/writer. See
# PROVENANCE.md for origin details and what is covered so far.
#
# Kept independent of the rest of Hazor: no Qt, no OpenCV, no core/ types.
# Self-sufficient CMake (its own find_package calls, its own test option),
# so it builds whether or not the root project has reached its own
# find_package/testing setup yet. Linked into the Hazor executable target
# so the normal build exercises it, but not yet wired into Hazor's document
# system — see PROVENANCE.md "Status".

find_package(ZLIB REQUIRED)
find_package(Threads REQUIRED)

add_library(psd STATIC
    Error.hpp
    Error.cpp
    Io.hpp
    Io.cpp
    Header.hpp
    Header.cpp
    Blend.hpp
    Blend.cpp
    Compression.hpp
    Compression.cpp
    ImageData.hpp
    ImageData.cpp
    Descriptor.hpp
    Descriptor.cpp
    Rect.hpp
    Rect.cpp
    FilterEffects.hpp
    FilterEffects.cpp
    Tagged.hpp
    Tagged.cpp
    Resources.hpp
    Resources.cpp
    Metadata.hpp
    Metadata.cpp
    Layer.hpp
    Layer.cpp
    Linked.hpp
    Linked.cpp
    File.hpp
    File.cpp
    Pixels.hpp
    Pixels.cpp
    Tree.hpp
    Tree.cpp
    Builder.hpp
    Builder.cpp
)

target_include_directories(psd PUBLIC ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(psd PRIVATE ZLIB::ZLIB Threads::Threads)
set_target_properties(psd PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

option(PSD_BUILD_TESTS "Build the psd library's own test suite" ON)
if(PSD_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
