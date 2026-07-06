#pragma once

#include "AppDiagnostics.hpp"

#include <QDialog>

class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget* parent = nullptr);

private:
    void setupUi();
    void applyStyleSheet();

    AppDiagnostics m_diag;
};
