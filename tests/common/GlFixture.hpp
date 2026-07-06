#pragma once

#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QSurfaceFormat>
#include <QOpenGLFunctions>

struct GlFixture {
    QOffscreenSurface surface;
    QOpenGLContext ctx;
    QOpenGLExtraFunctions* gl = nullptr;

    GlFixture(int glMajor = 3, int glMinor = 3) {
        QSurfaceFormat fmt;
        fmt.setVersion(glMajor, glMinor);
        fmt.setProfile(QSurfaceFormat::CoreProfile);
        surface.setFormat(fmt);
        surface.create();
        ctx.setFormat(fmt);
        ctx.create();
        bool ok = ctx.makeCurrent(&surface);
        Q_UNUSED(ok);
        QOpenGLContext::currentContext()->functions()->initializeOpenGLFunctions();
        gl = QOpenGLContext::currentContext()->extraFunctions();
    }
};
