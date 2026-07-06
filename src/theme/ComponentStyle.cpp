#include "ComponentStyle.hpp"

bool QtComponentStyle::isEmpty() const
{
    return !background.isValid()
        && !text.isValid()
        && !border.isValid()
        && !backgroundHover.isValid()
        && !backgroundPressed.isValid()
        && !backgroundDisabled.isValid()
        && !backgroundSelected.isValid()
        && !backgroundFocused.isValid()
        && !textDisabled.isValid()
        && !borderHover.isValid()
        && !borderFocused.isValid()
        && padding < 0
        && paddingTop < 0
        && paddingRight < 0
        && paddingBottom < 0
        && paddingLeft < 0
        && margin < 0
        && marginTop < 0
        && marginRight < 0
        && marginBottom < 0
        && marginLeft < 0
        && borderRadius < 0
        && borderWidth < 0
        && minHeight < 0
        && minWidth < 0
        && fontSize < 0
        && fontWeight < 0
        && fontFamily.isEmpty();
}

void QtComponentStyle::inherit(const QtComponentStyle& base)
{
    if (!background.isValid())    background    = base.background;
    if (!text.isValid())          text          = base.text;
    if (!border.isValid())        border        = base.border;
    if (!backgroundHover.isValid())    backgroundHover    = base.backgroundHover;
    if (!backgroundPressed.isValid())  backgroundPressed  = base.backgroundPressed;
    if (!backgroundDisabled.isValid()) backgroundDisabled = base.backgroundDisabled;
    if (!backgroundSelected.isValid()) backgroundSelected = base.backgroundSelected;
    if (!backgroundFocused.isValid())  backgroundFocused  = base.backgroundFocused;
    if (!textDisabled.isValid())       textDisabled       = base.textDisabled;
    if (!borderHover.isValid())        borderHover        = base.borderHover;
    if (!borderFocused.isValid())      borderFocused      = base.borderFocused;

    if (padding < 0)      padding      = base.padding;
    if (paddingTop < 0)   paddingTop   = base.paddingTop;
    if (paddingRight < 0) paddingRight = base.paddingRight;
    if (paddingBottom < 0)paddingBottom= base.paddingBottom;
    if (paddingLeft < 0)  paddingLeft  = base.paddingLeft;

    if (margin < 0)       margin       = base.margin;
    if (marginTop < 0)    marginTop    = base.marginTop;
    if (marginRight < 0)  marginRight  = base.marginRight;
    if (marginBottom < 0) marginBottom = base.marginBottom;
    if (marginLeft < 0)   marginLeft   = base.marginLeft;

    if (borderRadius < 0) borderRadius = base.borderRadius;
    if (borderWidth < 0)  borderWidth  = base.borderWidth;
    if (minHeight < 0)    minHeight    = base.minHeight;
    if (minWidth < 0)     minWidth     = base.minWidth;
    if (fontSize < 0)     fontSize     = base.fontSize;
    if (fontWeight < 0)   fontWeight   = base.fontWeight;
    if (fontFamily.isEmpty()) fontFamily = base.fontFamily;
}
