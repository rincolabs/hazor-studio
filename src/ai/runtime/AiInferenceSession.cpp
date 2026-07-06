#include "AiInferenceSession.hpp"

// AiInferenceSession is currently header-only (a thin owner of named
// OnnxSessionHandle sub-sessions). This translation unit exists so the type has
// a stable home for non-inline members as the inference pipeline grows in later
// stages.
