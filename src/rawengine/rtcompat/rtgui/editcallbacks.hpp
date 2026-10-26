#pragma once
// rtcompat/rtgui/editcallbacks.hpp
//
// Stand-in for rtgui/editcallbacks.h (+ rtgui/editid.h, folded in here
// since nothing in rtengine includes editid.h directly) and the
// CursorShape enum from rtgui/cursormanager.h (the only piece of that
// header rtengine needs — the rest is a Gtk::Cursor wrapper with no
// rtengine callers).
//
// EditSubscriber/EditDataProvider let rtengine hand "pipette" pixel
// values (color picker, white balance spot, …) back to an interactive
// editing UI. Hazor has no such UI yet: RawSession::renderPreview /
// renderFull (Fase 3) always pass nullptr for the EditDataProvider*
// parameter, so these classes are never actually subscribed to at
// runtime — they just need to exist with the real upstream logic so the
// nullptr-guarded call sites in pipettebuffer.cc / dcrop.cc /
// improcfun.cc / improccoordinator.cc compile and behave exactly as
// upstream does on that (always-taken, for now) nullptr path.
#include <vector>

#include "rtengine/coord.h"

class Geometry; // GUI-only drawing primitive; rtengine only stores pointers to it, never dereferences one.
class EditDataProvider;

// --- rtgui/editid.h ------------------------------------------------------

enum EditUniqueID : int {
    EUID_None,

    EUID_ToneCurve1,
    EUID_ToneCurve2,
    EUID_Lab_LCurve,
    EUID_Lab_CCurve,
    EUID_Lab_LCCurve,
    EUID_Lab_CLCurve,
    EUID_Lab_LHCurve,
    EUID_Lab_CHCurve,
    EUID_Lab_HHCurve,
    EUID_Lab_aCurve,
    EUID_Lab_bCurve,
    EUID_RGB_R,
    EUID_RGB_G,
    EUID_RGB_B,
    EUID_HSV_H,
    EUID_HSV_S,
    EUID_HSV_V,
    EUID_BlackWhiteLuminance,
    EUID_BlackWhiteBeforeCurve,
    EUID_BlackWhiteAfterCurve,
    EUID_WW_HHCurve,
};

enum EditType {
    ET_PIPETTE,
    ET_OBJECTS
};

enum BufferType {
    BT_IMAGEFLOAT,
    BT_LABIMAGE,
    BT_SINGLEPLANE_FLOAT
};

enum ObjectMode {
    OM_255,
    OM_65535
};

// --- rtgui/cursormanager.h (CursorShape only) -----------------------------

enum CursorShape {
    CSAddColPicker,
    CSArrow,
    CSCropSelect,
    CSCrosshair,
    CSEmpty,
    CSHandClosed,
    CSHandOpen,
    CSMove,
    CSMove1DH,
    CSMove1DV,
    CSMove2D,
    CSMoveLeft,
    CSMoveRight,
    CSMoveRotate,
    CSPlus,
    CSResizeBottomLeft,
    CSResizeBottomRight,
    CSResizeDiagonal,
    CSResizeHeight,
    CSResizeTopLeft,
    CSResizeTopRight,
    CSResizeWidth,
    CSSpotWB,
    CSStraighten,
    CSUndefined,
    CSWait
};

// --- rtgui/editcallbacks.h ------------------------------------------------

class EditSubscriber
{
public:
    explicit EditSubscriber(EditType editType);
    virtual ~EditSubscriber() = default;

    void setEditProvider(EditDataProvider* provider);
    EditDataProvider* getEditProvider() const;
    void setEditID(EditUniqueID id, BufferType buffType);
    bool isCurrentSubscriber() const;
    virtual void subscribe();
    virtual void unsubscribe();
    virtual void switchOffEditMode();
    EditUniqueID getEditID() const;
    EditType getEditingType() const;
    BufferType getPipetteBufferType() const;
    bool isDragging() const;
    bool isPicking() const;

    virtual CursorShape getCursor(int objectID, int xPos, int yPos) const;
    virtual bool mouseOver(int modifierKey);
    virtual bool button1Pressed(int modifierKey);
    virtual bool button1Released();
    virtual bool button2Pressed(int modifierKey);
    virtual bool button2Released();
    virtual bool button3Pressed(int modifierKey);
    virtual bool button3Released();
    virtual bool drag1(int modifierKey);
    virtual bool drag2(int modifierKey);
    virtual bool drag3(int modifierKey);
    virtual bool pick1(bool picked);
    virtual bool pick2(bool picked);
    virtual bool pick3(bool picked);

    const std::vector<Geometry*>& getVisibleGeometry();
    const std::vector<Geometry*>& getMouseOverGeometry();

private:
    EditUniqueID ID;
    EditType editingType;
    BufferType bufferType;
    EditDataProvider* provider;

protected:
    std::vector<Geometry*> visibleGeometry;
    std::vector<Geometry*> mouseOverGeometry;
    enum class Action { NONE, DRAGGING, PICKING };
    Action action;
};

class EditDataProvider
{
public:
    int object;
    rtengine::Coord posScreen;
    rtengine::Coord posImage;
    rtengine::Coord deltaScreen;
    rtengine::Coord deltaImage;
    rtengine::Coord deltaPrevScreen;
    rtengine::Coord deltaPrevImage;

    EditDataProvider();
    virtual ~EditDataProvider() = default;

    virtual void subscribe(EditSubscriber* subscriber);
    virtual void unsubscribe();
    virtual void switchOffEditMode();
    int getObject() const;
    void setObject(int newObject);
    float getPipetteVal1() const;
    float getPipetteVal2() const;
    float getPipetteVal3() const;
    void setPipetteVal1(float newVal);
    void setPipetteVal2(float newVal);
    void setPipetteVal3(float newVal);
    virtual CursorShape getCursor(int objectID, int xPos, int yPos) const;
    int getPipetteRectSize() const;
    EditSubscriber* getCurrSubscriber() const;
    virtual void getImageSize(int& w, int& h) = 0;
    virtual void getPreviewCenterPos(int& x, int& y) = 0;
    virtual void getPreviewSize(int& w, int& h) = 0;

private:
    EditSubscriber* currSubscriber;
    float pipetteVal1;
    float pipetteVal2;
    float pipetteVal3;
};
