#pragma once

#include <QByteArray>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QRegularExpression>
#include <QString>

namespace shader_compat {

struct ShaderFlavor {
    QByteArray versionHeader;
    bool usesLayoutLocations = false;
};

inline ShaderFlavor currentFlavor()
{
    auto* ctx = QOpenGLContext::currentContext();
    if (!ctx)
        return {QByteArrayLiteral("#version 130\n"), false};

    if (ctx->isOpenGLES()) {
        return {
            QByteArrayLiteral(
                "#version 300 es\n"
                "precision mediump float;\n"
                "precision mediump int;\n"),
            true
        };
    }

    const QSurfaceFormat fmt = ctx->format();
    if (fmt.majorVersion() > 3 || (fmt.majorVersion() == 3 && fmt.minorVersion() >= 3))
        return {QByteArrayLiteral("#version 330 core\n"), true};

    return {QByteArrayLiteral("#version 130\n"), false};
}

inline QByteArray adaptSource(const char* source)
{
    const ShaderFlavor flavor = currentFlavor();
    QString text = QString::fromUtf8(source);

    text.remove(QRegularExpression(QStringLiteral("^\\s*#version\\s+330\\s+core\\s*\\n")));
    if (!flavor.usesLayoutLocations) {
        text.replace(
            QRegularExpression(QStringLiteral("layout\\s*\\(\\s*location\\s*=\\s*\\d+\\s*\\)\\s*")),
            QString());
    }

    QByteArray out = flavor.versionHeader;
    out += text.toUtf8();
    return out;
}

inline void bindQuadAttributes(QOpenGLShaderProgram* program)
{
    program->bindAttributeLocation("pos", 0);
    program->bindAttributeLocation("uv", 1);
}

inline void bindInstancedAttributes(QOpenGLShaderProgram* program)
{
    bindQuadAttributes(program);
    program->bindAttributeLocation("aMvp0", 2);
    program->bindAttributeLocation("aMvp1", 3);
    program->bindAttributeLocation("aMvp2", 4);
    program->bindAttributeLocation("aMvp3", 5);
    program->bindAttributeLocation("aUvOffset", 6);
    program->bindAttributeLocation("aUvScale", 7);
}

} // namespace shader_compat
