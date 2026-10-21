#include "CanvasColorPickSource.hpp"

namespace {
CanvasColorPickSource::Resolver g_resolver;
}

void CanvasColorPickSource::setResolver(Resolver resolver)
{
    g_resolver = std::move(resolver);
}

CanvasView* CanvasColorPickSource::canvas()
{
    return g_resolver ? g_resolver() : nullptr;
}
