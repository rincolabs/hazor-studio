#pragma once

// Internal to src/raw/ — the real shape of RawDevelopParams::Impl, shared by
// RawDevelopParams.cpp and RawSession.cpp (which needs the rtengine ProcParams
// out of a RawDevelopParams to drive StagedImageProcessor/ProcessingJob).
// Never included from outside src/raw/*.cpp, and only ever under
// HAVE_RAWENGINE (see RawEngineAvailability.hpp).
//
// Include "raw/RawDevelopParams.hpp" before this header.

#include "rtengine/procparams.h"

struct RawDevelopParams::Impl {
    rtengine::procparams::ProcParams procParams;
};
