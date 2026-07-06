#pragma once

#include "ai/inpaint/AiInpaintTypes.hpp"

#include <QString>

class AiInpaintPipeline {
public:
    AiInpaintResult runRemoveObject(const AiInpaintRequest& request);
};
