#include "TextRenderer.hpp"
#include "TextLayoutEngine.hpp"
#include <QPainter>
#include <algorithm>
#include <cmath>

void TextRenderer::render(TextLayerData& data, QImage& outImage)
{
    if (data.text.isEmpty() && !m_editing) {
        outImage = QImage(1, 1, QImage::Format_RGBA8888);
        outImage.fill(Qt::transparent);
        data.dirty = false;
        return;
    }

    TextLayoutEngine engine;
    engine.setData(&data);
    engine.layout(m_editing);

    QSizeF content = engine.contentSize(m_editing);
    int imgW = std::max(1, static_cast<int>(std::ceil(content.width())));
    int imgH = std::max(1, static_cast<int>(std::ceil(content.height())));

    if (data.flowMode == TextFlowMode::Paragraph && data.box.width > 0.0f)
        imgW = static_cast<int>(std::ceil(data.box.width));
    if (data.flowMode == TextFlowMode::Paragraph && data.box.height > 0.0f)
        imgH = static_cast<int>(std::ceil(data.box.height));

    imgW += 12;
    imgH += 12;

    QImage img(imgW, imgH, QImage::Format_RGBA8888);
    img.fill(Qt::transparent);

    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    engine.drawText(painter, data, m_editing, m_cursorPos,
                    m_selStart, m_selEnd, m_caretVisible);

    painter.end();

    outImage = img;
    data.dirty = false;
}
