# One Boost.Test executable per ported module, mirroring each Rust module's
# colocated `#[cfg(test)] mod tests`, plus the integration tests ported from
# the crate's own `tests/*.rs` (see PROVENANCE.md). Boost.Test is used
# "included" (single header, no link step), matching
# tests/unit/test_imageengine.cpp at the repo root.

find_package(Boost REQUIRED)

# Testgen is a deterministic synthetic-file generator used by many
# integration tests, same role as the crate's own `testgen` module. Built as
# its own static lib so only the test executables that need it link it.
add_library(psd_testgen STATIC
    Testgen.hpp
    Testgen.cpp
)
target_link_libraries(psd_testgen PUBLIC psd)
target_include_directories(psd_testgen PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(psd_testgen PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)

function(psd_add_test name)
    add_executable(test-psd-${name} Test${name}.cpp)
    target_include_directories(test-psd-${name} PRIVATE ${Boost_INCLUDE_DIRS})
    target_link_libraries(test-psd-${name} PRIVATE psd)
    set_target_properties(test-psd-${name} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    add_test(NAME psd-${name} COMMAND test-psd-${name})
endfunction()

function(psd_add_test_with_testgen name)
    add_executable(test-psd-${name} Test${name}.cpp)
    target_include_directories(test-psd-${name} PRIVATE ${Boost_INCLUDE_DIRS})
    target_link_libraries(test-psd-${name} PRIVATE psd_testgen)
    set_target_properties(test-psd-${name} PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    add_test(NAME psd-${name} COMMAND test-psd-${name})
endfunction()

psd_add_test(Io)
psd_add_test(Header)
psd_add_test(Blend)
psd_add_test(Compression)
psd_add_test(Descriptor)
psd_add_test(FilterEffects)
psd_add_test(Tagged)
psd_add_test(Resources)
psd_add_test(Metadata)
psd_add_test_with_testgen(Layer)
psd_add_test_with_testgen(Pixels)
psd_add_test(Builder)
psd_add_test_with_testgen(File)
psd_add_test_with_testgen(Tree)
