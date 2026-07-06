#pragma once

#include <QComboBox>

// Thin themed combo used across the app's panels. It deliberately carries no
// stylesheet of its own: it inherits the global application theme (exactly like
// the Layers blend-mode combo) so every combo looks identical. Kept as a named
// type so shared combo behavior can live here later.
class AppComboBox : public QComboBox {
    Q_OBJECT

public:
    explicit AppComboBox(QWidget* parent = nullptr);
};
