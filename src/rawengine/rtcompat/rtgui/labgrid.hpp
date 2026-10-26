#pragma once
// rtcompat/rtgui/labgrid.hpp
//
// Stand-in for rtgui/labgrid.h. That header is mostly the GTK LabGridArea
// widget (Color Toning / ICM tool's drag-a-point-on-a-plane control), which
// has no rtengine callers — but it also defines two free constexpr floats
// that iplab2rgb.cc/improccoordinator.cc *do* use directly, for mapping
// primary/CIExy coordinates onto Labgrid's [-1, 1] widget space. Only
// those two are reproduced here.
namespace rtengine
{

constexpr float OFFSET_MODIFIER = 1.81818f; // Scaling coefficient of primary data and CIExy diagram with that of Labgrid.
constexpr float CIExy_MARGIN = 0.1f; // corresponds to the left and bottom margin on the CIExy diagram

} // namespace rtengine
