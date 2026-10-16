#include "rtcompat/rtgui/editcallbacks.hpp"

EditSubscriber::EditSubscriber(EditType editType) :
    ID(EUID_None),
    editingType(editType),
    bufferType(BT_SINGLEPLANE_FLOAT),
    provider(nullptr),
    action(EditSubscriber::Action::NONE)
{
}

void EditSubscriber::setEditProvider(EditDataProvider* provider_)
{
    provider = provider_;
}

EditDataProvider* EditSubscriber::getEditProvider() const
{
    return provider;
}

void EditSubscriber::setEditID(EditUniqueID id, BufferType buffType)
{
    ID = id;
    bufferType = buffType;
}

bool EditSubscriber::isCurrentSubscriber() const
{
    if (provider) {
        return provider->getCurrSubscriber() == this;
    }
    return false;
}

void EditSubscriber::subscribe()
{
    if (provider) {
        provider->subscribe(this);
    }
}

void EditSubscriber::unsubscribe()
{
    if (provider) {
        provider->unsubscribe();
    }
}

void EditSubscriber::switchOffEditMode()
{
    unsubscribe();
}

EditUniqueID EditSubscriber::getEditID() const
{
    return ID;
}

EditType EditSubscriber::getEditingType() const
{
    return editingType;
}

BufferType EditSubscriber::getPipetteBufferType() const
{
    return bufferType;
}

bool EditSubscriber::isDragging() const
{
    return action == EditSubscriber::Action::DRAGGING;
}

bool EditSubscriber::isPicking() const
{
    return action == EditSubscriber::Action::PICKING;
}

CursorShape EditSubscriber::getCursor(int /*objectID*/, int /*xPos*/, int /*yPos*/) const
{
    return CSHandOpen;
}

bool EditSubscriber::mouseOver(int /*modifierKey*/) { return false; }
bool EditSubscriber::button1Pressed(int /*modifierKey*/) { return false; }
bool EditSubscriber::button1Released() { return false; }
bool EditSubscriber::button2Pressed(int /*modifierKey*/) { return false; }
bool EditSubscriber::button2Released() { return false; }
bool EditSubscriber::button3Pressed(int /*modifierKey*/) { return false; }
bool EditSubscriber::button3Released() { return false; }
bool EditSubscriber::drag1(int /*modifierKey*/) { return false; }
bool EditSubscriber::drag2(int /*modifierKey*/) { return false; }
bool EditSubscriber::drag3(int /*modifierKey*/) { return false; }
bool EditSubscriber::pick1(bool /*picked*/) { return false; }
bool EditSubscriber::pick2(bool /*picked*/) { return false; }
bool EditSubscriber::pick3(bool /*picked*/) { return false; }

const std::vector<Geometry*>& EditSubscriber::getVisibleGeometry()
{
    return visibleGeometry;
}

const std::vector<Geometry*>& EditSubscriber::getMouseOverGeometry()
{
    return mouseOverGeometry;
}

//--------------------------------------------------------------------------

EditDataProvider::EditDataProvider() :
    object(0),
    posScreen(-1, -1),
    posImage(-1, -1),
    deltaScreen(0, 0),
    deltaImage(0, 0),
    deltaPrevScreen(0, 0),
    deltaPrevImage(0, 0),
    currSubscriber(nullptr),
    pipetteVal1(0.f),
    pipetteVal2(0.f),
    pipetteVal3(0.f)
{
}

void EditDataProvider::subscribe(EditSubscriber* subscriber)
{
    if (currSubscriber) {
        currSubscriber->switchOffEditMode();
    }
    currSubscriber = subscriber;
}

void EditDataProvider::unsubscribe()
{
    currSubscriber = nullptr;
}

void EditDataProvider::switchOffEditMode()
{
    if (currSubscriber) {
        currSubscriber->switchOffEditMode();
    }
}

int EditDataProvider::getObject() const
{
    return object;
}

void EditDataProvider::setObject(int newObject)
{
    object = newObject;
}

float EditDataProvider::getPipetteVal1() const { return pipetteVal1; }
float EditDataProvider::getPipetteVal2() const { return pipetteVal2; }
float EditDataProvider::getPipetteVal3() const { return pipetteVal3; }
void EditDataProvider::setPipetteVal1(float newVal) { pipetteVal1 = newVal; }
void EditDataProvider::setPipetteVal2(float newVal) { pipetteVal2 = newVal; }
void EditDataProvider::setPipetteVal3(float newVal) { pipetteVal3 = newVal; }

CursorShape EditDataProvider::getCursor(int objectID, int xPos, int yPos) const
{
    if (currSubscriber) {
        currSubscriber->getCursor(objectID, xPos, yPos);
    }
    return CSHandOpen;
}

int EditDataProvider::getPipetteRectSize() const
{
    return 8;
}

EditSubscriber* EditDataProvider::getCurrSubscriber() const
{
    return currSubscriber;
}
