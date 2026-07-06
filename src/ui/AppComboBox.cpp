#include "AppComboBox.hpp"

AppComboBox::AppComboBox(QWidget* parent)
    : QComboBox(parent)
{
    // Intentionally unstyled — it inherits the global application theme so it
    // matches the standard combos (e.g. the Layers blend-mode combo).
}
