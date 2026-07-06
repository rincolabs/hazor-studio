#pragma once

#include "GradientTypes.hpp"
#include "controller/CommandHistory.hpp"

#include <QImage>
#include <QTransform>

class Document;

class ApplyGradientCommand : public Command {
public:
    ApplyGradientCommand(Document* doc,
                         int flatIndex,
                         QImage before,
                         QImage after,
                         GradientApplication application,
                         QString name = QStringLiteral("Apply Gradient"));

    void execute() override;
    void undo() override;
    QString name() const override { return m_name; }

    const GradientApplication& application() const { return m_application; }

private:
    void apply(const QImage& image);

    Document* m_doc = nullptr;
    int m_flatIndex = -1;
    QImage m_before;
    QImage m_after;
    GradientApplication m_application;
    QString m_name;
};

