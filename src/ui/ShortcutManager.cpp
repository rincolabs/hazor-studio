#include "ShortcutManager.hpp"

#include <QAction>
#include <QKeyEvent>
#include <QShortcut>
#include <QSettings>
#include <QSet>

namespace {
const QString kClearedShortcutMarker = QStringLiteral("__none__");
}

ShortcutManager* ShortcutManager::instance()
{
    static ShortcutManager inst;
    return &inst;
}

ShortcutManager::ShortcutManager(QObject* parent)
    : QObject(parent)
{
    buildDefaults();
    loadSettings();
}

void ShortcutManager::buildDefaults()
{
    auto add = [&](const QString& id, const QString& cat, const QString& label,
                   const QKeySequence& seq = {},
                   ShortcutScope scope = ShortcutScope::Global,
                   const QVector<QKeySequence>& alternatives = {}) {
        m_actions.push_back({id, cat, label, seq, alternatives, scope});
        m_actionIndex[id] = m_actions.size() - 1;
    };

    // ── File ──
    add("file.new",             tr("File"), tr("&New..."),              QKeySequence::New);
    add("file.open",            tr("File"), tr("&Open..."),             QKeySequence::Open);
    add("file.save",            tr("File"), tr("&Save..."),             QKeySequence::Save);
    add("file.save_as",         tr("File"), tr("Save &As..."),          QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S));
    add("file.export_as",       tr("File"), tr("&Export As..."),         QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E));
    add("file.import",          tr("File"), tr("&Import Image..."),      QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I));
    add("file.exit",            tr("File"), tr("E&xit"),                QKeySequence::Quit);

    // ── Edit ──
    add("edit.undo",            tr("Edit"), tr("&Undo"),                QKeySequence::Undo);
    add("edit.redo",            tr("Edit"), tr("&Redo"),                QKeySequence::Redo);
    add("edit.copy",            tr("Edit"), tr("&Copy"),                QKeySequence::Copy);
    add("edit.paste",           tr("Edit"), tr("&Paste"),               QKeySequence::Paste);
    add("edit.cut",             tr("Edit"), tr("Cu&t"),                 QKeySequence::Cut);
    add("edit.free_transform",  tr("Edit"), tr("Free &Transform"),      QKeySequence(Qt::CTRL | Qt::Key_T));
    add("edit.delete_clear",    tr("Edit"), tr("&Delete / Clear"),      QKeySequence(Qt::Key_Delete));
    add("edit.document",        tr("Edit"), tr("&Document..."),         {});
    add("edit.configure_agent", tr("Edit"), tr("Configure &Agent..."),  {});
    add("edit.settings",        tr("Edit"), tr("&Settings..."),         QKeySequence(Qt::CTRL | Qt::Key_Comma));

    // ── Layer ──
    add("layer.new",            tr("Layer"), tr("&New Layer"),           QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_N), ShortcutScope::GlobalSafe);
    add("layer.remove",         tr("Layer"), tr("&Remove Layer"),        QKeySequence(Qt::SHIFT | Qt::Key_Delete),       ShortcutScope::LayerPanelOrCanvas);
    add("layer.duplicate",      tr("Layer"), tr("&Duplicate Layer"),     QKeySequence(Qt::CTRL | Qt::Key_J),             ShortcutScope::GlobalSafe);
    add("layer.fill",           tr("Layer"), tr("&Fill Layer..."),       QKeySequence(Qt::SHIFT | Qt::Key_Backspace),    ShortcutScope::Canvas);
    add("layer.merge_visible",  tr("Layer"), tr("&Merge Visible"),       QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_E), ShortcutScope::GlobalSafe);
    add("layer.merge_down",     tr("Layer"), tr("Merge &Down"),          QKeySequence(Qt::CTRL | Qt::Key_E),             ShortcutScope::GlobalSafe);
    add("layer.merge_layers",   tr("Layer"), tr("&Merge Layers"),        {}, ShortcutScope::LayerPanelOrCanvas);
    add("layer.flatten_image",  tr("Layer"), tr("&Flatten Image"),       {}, ShortcutScope::GlobalSafe);
    add("layer.rasterize",      tr("Layer"), tr("Rasterize &Layer"),    {}, ShortcutScope::LayerPanel);
    add("layer.styles",         tr("Layer"), tr("Layer &Styles..."),     {}, ShortcutScope::LayerPanel);
    add("layer.flip_h",         tr("Layer"), tr("Flip &Horizontal"),    {}, ShortcutScope::LayerPanelOrCanvas);
    add("layer.flip_v",         tr("Layer"), tr("Flip &Vertical"),      {}, ShortcutScope::LayerPanelOrCanvas);
    add("layer.resize",         tr("Layer"), tr("&Resize Layer..."),    {}, ShortcutScope::LayerPanel);

    // ── Layer structure ──
    add("layer.new.quick",                  tr("Layer"), tr("New &Layer (no dialog)"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::SHIFT | Qt::Key_N), ShortcutScope::GlobalSafe);
    add("layer.new.group",                  tr("Layer"), tr("New &Group"),             {}, ShortcutScope::LayerPanel);
    add("layer.group.create_from_selection", tr("Layer"), tr("&Group Layers"),          QKeySequence(Qt::CTRL | Qt::Key_G),             ShortcutScope::GlobalSafe);
    add("layer.group.ungroup",              tr("Layer"), tr("&Ungroup Layers"),         QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G), ShortcutScope::GlobalSafe);
    add("layer.arrange.forward",            tr("Layer"), tr("Bring &Forward"),          QKeySequence(Qt::CTRL | Qt::Key_BracketRight),  ShortcutScope::GlobalSafe);
    add("layer.arrange.backward",           tr("Layer"), tr("Send &Backward"),          QKeySequence(Qt::CTRL | Qt::Key_BracketLeft),   ShortcutScope::GlobalSafe);
    add("layer.arrange.to_front",           tr("Layer"), tr("Bring to Fro&nt"),         QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketRight), ShortcutScope::GlobalSafe);
    add("layer.arrange.to_back",            tr("Layer"), tr("Send to Bac&k"),           QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_BracketLeft),  ShortcutScope::GlobalSafe);
    add("layer.select.above",               tr("Layer"), tr("Select Layer &Above"),     QKeySequence(Qt::ALT | Qt::Key_BracketRight),   ShortcutScope::GlobalSafe);
    add("layer.select.below",               tr("Layer"), tr("Select Layer Belo&w"),     QKeySequence(Qt::ALT | Qt::Key_BracketLeft),    ShortcutScope::GlobalSafe);
    add("layer.select.top",                 tr("Layer"), tr("Select &Top Layer"),       QKeySequence(Qt::ALT | Qt::Key_Period),         ShortcutScope::GlobalSafe);
    add("layer.select.bottom",              tr("Layer"), tr("Select Bottom Layer"),     QKeySequence(Qt::ALT | Qt::Key_Comma),          ShortcutScope::GlobalSafe);
    add("layer.select.all",                 tr("Layer"), tr("Select All La&yers"),      QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_A),   ShortcutScope::GlobalSafe);

    // ── Layer locks ──
    add("layer.lock.transparent.toggle", tr("Layer"), tr("Lock &Transparent Pixels"), QKeySequence(Qt::Key_Slash), ShortcutScope::LayerPanelOrMoveTool);
    add("layer.lock.image.toggle",       tr("Layer"), tr("Lock &Image Pixels"),       {}, ShortcutScope::LayerPanel);
    add("layer.lock.position.toggle",    tr("Layer"), tr("Lock &Position"),           {}, ShortcutScope::LayerPanel);
    add("layer.lock.all.toggle",         tr("Layer"), tr("Lock &All"),                {}, ShortcutScope::LayerPanel);

    // ── Clipping / Single-Layer-Mode ──
    add("layer.clipping.toggle",         tr("Layer"), tr("Create/Release &Clipping Mask"), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_G), ShortcutScope::GlobalSafe);

    // ── Layer effects / styles ──
    add("layer.effects.copy",       tr("Layer Style"), tr("&Copy Layer Style"),       {}, ShortcutScope::LayerPanel);
    add("layer.effects.paste",      tr("Layer Style"), tr("&Paste Layer Style"),      {}, ShortcutScope::LayerPanel);
    add("layer.effects.clear",      tr("Layer Style"), tr("C&lear Layer Style"),      {}, ShortcutScope::LayerPanel);
    add("layer.effects.toggle_all", tr("Layer Style"), tr("&Enable/Disable Effects"), {}, ShortcutScope::LayerPanel);

    // ── Layer Mask ──
    add("mask.add",             tr("Layer Mask"), tr("&Add Mask"),       {}, ShortcutScope::LayerPanel);
    add("mask.remove",          tr("Layer Mask"), tr("&Remove Mask"),    {}, ShortcutScope::LayerPanel);
    add("mask.apply",           tr("Layer Mask"), tr("&Apply Mask"),     {}, ShortcutScope::LayerPanel);
    add("mask.toggle",          tr("Layer Mask"), tr("&Enable/Disable"), {}, ShortcutScope::LayerPanel);
    add("mask.from_selection",  tr("Layer Mask"), tr("From &Selection"), {}, ShortcutScope::LayerPanel);
    add("mask.invert",          tr("Layer Mask"), tr("&Invert Mask"),    {}, ShortcutScope::MaskTarget);
    add("mask.copy",            tr("Layer Mask"), tr("&Copy Mask"),      {}, ShortcutScope::LayerPanel);
    add("mask.paste",           tr("Layer Mask"), tr("&Paste Mask"),     {}, ShortcutScope::LayerPanel);
    add("mask.clear",           tr("Layer Mask"), tr("C&lear Mask"),     {}, ShortcutScope::LayerPanel);

    // ── Image / Adjust ──
    add("image.adjust",         tr("Image"), tr("&Color Adjustments..."), {});
    add("image.auto_contrast",  tr("Image"), tr("&Auto Contrast"),        {});

    // ── Image / Filter ──
    add("image.gaussian_blur",  tr("Image"), tr("&Gaussian Blur..."),     {});
    add("image.box_blur",       tr("Image"), tr("Box Blur..."),          {});
    add("image.median_blur",    tr("Image"), tr("&Median Blur..."),       {});
    add("image.bilateral_blur", tr("Image"), tr("Bilateral Blur..."),     {});
    add("image.motion_blur",    tr("Image"), tr("Motion Blur..."),        {});
    add("image.radial_blur",    tr("Image"), tr("Radial Blur..."),        {});
    add("image.zoom_blur",      tr("Image"), tr("Zoom Blur..."),          {});
    add("image.sharpen",        tr("Image"), tr("&Sharpen..."),           {});
    add("image.posterize",      tr("Image"), tr("&Posterize..."),         {});
    add("image.threshold",      tr("Image"), tr("&Threshold..."),         {});
    add("image.edges",          tr("Image"), tr("&Edges..."),             {});
    add("image.grayscale",      tr("Image"), tr("&Grayscale"),            {});
    add("image.invert",         tr("Image"), tr("&Invert Colors"),        {});
    // Contextual Ctrl+I (Etapa 2): on the canvas this inverts the active mask
    // when the mask is the edit target, otherwise inverts the layer pixels. In a
    // text edit it never fires (Canvas scope = -1 there) so Ctrl+I stays Italic.
    add("edit.invert_contextual", tr("Image"), tr("In&vert (Mask/Colors)"), QKeySequence(Qt::CTRL | Qt::Key_I), ShortcutScope::Canvas);
    add("image.noise_reduce",   tr("Image"), tr("&Noise Reduce..."),      {});
    add("image.remove_bg",      tr("Image"), tr("&Remove Background..."), {});

    // ── Selection ──
    add("select.all",           tr("Selection"), tr("&Select All"),          QKeySequence::SelectAll,                    ShortcutScope::Canvas);
    add("select.deselect",      tr("Selection"), tr("&Deselect"),            QKeySequence(Qt::CTRL | Qt::Key_D),         ShortcutScope::Canvas);
    add("select.invert",        tr("Selection"), tr("&Invert Selection"),    QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_I), ShortcutScope::Canvas);
    add("select.refine",        tr("Selection"), tr("&Refine Selection..."), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_R), ShortcutScope::Canvas);
    add("selection.generative_fill", tr("Selection"), tr("&Generative Fill"), {}, ShortcutScope::Canvas);
    add("selection.crop_to_selection", tr("Selection"), tr("&Crop To Selection"), {}, ShortcutScope::Canvas);
    add("select.feather",       tr("Selection"), tr("&Feather..."),          QKeySequence(Qt::SHIFT | Qt::Key_F6),        ShortcutScope::Canvas);
    add("select.grow",          tr("Selection"), tr("&Grow"),                {}, ShortcutScope::Canvas);
    add("select.shrink",        tr("Selection"), tr("&Shrink"),              {}, ShortcutScope::Canvas);
    add("select.border",        tr("Selection"), tr("&Border"),              {}, ShortcutScope::Canvas);
    add("select.smooth",        tr("Selection"), tr("&Smooth"),              {}, ShortcutScope::Canvas);

    // ── Tools ──
    add("tool.move",            tr("Tools"), tr("&Move Tool"),             QKeySequence(Qt::Key_V),                 ShortcutScope::Tool);
    add("tool.brush",           tr("Tools"), tr("&Brush Tool"),            QKeySequence(Qt::Key_B),                 ShortcutScope::Tool);
    add("tool.eraser",          tr("Tools"), tr("&Eraser Tool"),           QKeySequence(Qt::Key_E),                 ShortcutScope::Tool);
    add("tool.select",          tr("Tools"), tr("&Selection Tool"),        QKeySequence(),                          ShortcutScope::Tool);
    add("tool.zoom",            tr("Tools"), tr("&Zoom Tool"),             QKeySequence(Qt::Key_Z),                 ShortcutScope::Tool);
    add("tool.hand",            tr("Tools"), tr("&Hand Tool"),             QKeySequence(Qt::Key_H),                 ShortcutScope::Tool);
    add("tool.text",            tr("Tools"), tr("&Text Tool"),             QKeySequence(Qt::Key_T),                 ShortcutScope::Tool);
    add("tool.crop",            tr("Tools"), tr("&Crop Tool"),             QKeySequence(Qt::Key_C),                 ShortcutScope::Tool);
    add("tool.fill_bucket",     tr("Tools"), tr("&Fill Bucket"),           QKeySequence(Qt::SHIFT | Qt::Key_G),     ShortcutScope::Tool);
    add("tool.eyedropper",      tr("Tools"), tr("&Eyedropper"),            QKeySequence(Qt::Key_I),                 ShortcutScope::Tool);
    add("tool.shape",           tr("Tools"), tr("&Shape Tool"),            QKeySequence(Qt::Key_U),                 ShortcutScope::Tool);
    add("tool.clone_stamp",     tr("Tools"), tr("&Clone Stamp Tool"),      QKeySequence(Qt::Key_S),                 ShortcutScope::Tool);
    add("tool.gradient",        tr("Tools"), tr("&Gradient Tool"),         QKeySequence(Qt::Key_G),                 ShortcutScope::Tool);
    add("tool.skew",            tr("Tools"), tr("&Skew Tool"),             QKeySequence(Qt::SHIFT | Qt::Key_V),     ShortcutScope::Tool);
    add("tool.ai_select",       tr("Tools"), tr("&AI Object Selection"),   QKeySequence(Qt::Key_W),                 ShortcutScope::Tool);
    // Temporarily hidden: AI Remove Tool will be released later.
    // Do not remove the implementation. Only the user-facing UI is disabled.
    // add("tool.ai_remove",       tr("Tools"), tr("AI &Remove Object"),      QKeySequence(),                          ShortcutScope::Tool);
    add("tool.healing_brush",   tr("Tools"), tr("&Healing Brush"),         QKeySequence(Qt::Key_J),                 ShortcutScope::Tool);
    add("tool.selection.marquee_rect",     tr("Tools"), tr("&Rectangular Marquee"), QKeySequence(Qt::Key_M),             ShortcutScope::Tool);
    add("tool.selection.marquee_ellipse",  tr("Tools"), tr("&Elliptical Marquee"),  QKeySequence(Qt::SHIFT | Qt::Key_M), ShortcutScope::Tool);
    add("tool.selection.lasso",            tr("Tools"), tr("&Lasso Tool"),          QKeySequence(Qt::Key_L),             ShortcutScope::Tool);
    add("tool.selection.polygonal_lasso",  tr("Tools"), tr("&Polygonal Lasso"),     QKeySequence(Qt::SHIFT | Qt::Key_L), ShortcutScope::Tool);
    add("tool.selection.magnetic_lasso",   tr("Tools"), tr("&Magnetic Lasso"),      QKeySequence(),                      ShortcutScope::Tool);
    add("tool.selection.quick_selection",  tr("Tools"), tr("&Quick Selection"),     QKeySequence(Qt::SHIFT | Qt::Key_W), ShortcutScope::Tool);
    add("tool.selection.magic_wand",       tr("Tools"), tr("&Magic Wand"),          QKeySequence(),                      ShortcutScope::Tool);

    // ── Text editing ──
    add("text.bold",            tr("Text"), tr("&Bold"),                   QKeySequence(Qt::CTRL | Qt::Key_B),      ShortcutScope::TextEditing);
    add("text.italic",          tr("Text"), tr("&Italic"),                 QKeySequence(Qt::CTRL | Qt::Key_I),      ShortcutScope::TextEditing);
    add("text.underline",       tr("Text"), tr("&Underline"),              QKeySequence(Qt::CTRL | Qt::Key_U),      ShortcutScope::TextEditing);
    add("text.strikethrough",   tr("Text"), tr("&Strikethrough"),          {},                                      ShortcutScope::TextEditing);

    // ── Brush controls ──
    add("brush.size_decrease",      tr("Brush"), tr("Decrease Brush &Size"),     QKeySequence(Qt::Key_BracketLeft),          ShortcutScope::ToolOption);
    add("brush.size_increase",      tr("Brush"), tr("Increase Brush &Size"),     QKeySequence(Qt::Key_BracketRight),         ShortcutScope::ToolOption);
    add("brush.hardness_decrease",  tr("Brush"), tr("Decrease Brush &Hardness"), QKeySequence(Qt::SHIFT | Qt::Key_BracketLeft),  ShortcutScope::ToolOption);
    add("brush.hardness_increase",  tr("Brush"), tr("Increase Brush &Hardness"), QKeySequence(Qt::SHIFT | Qt::Key_BracketRight), ShortcutScope::ToolOption);
    add("brush.preset_previous",    tr("Brush"), tr("Previous Brush Preset"),    QKeySequence(Qt::Key_Comma),               ShortcutScope::ToolOption);
    add("brush.preset_next",        tr("Brush"), tr("Next Brush Preset"),        QKeySequence(Qt::Key_Period),              ShortcutScope::ToolOption);
    add("brush.preset_first",       tr("Brush"), tr("First Brush Preset"),       QKeySequence(Qt::SHIFT | Qt::Key_Comma),   ShortcutScope::ToolOption);
    add("brush.preset_last",        tr("Brush"), tr("Last Brush Preset"),        QKeySequence(Qt::SHIFT | Qt::Key_Period),  ShortcutScope::ToolOption);
    add("panel.brush_settings.toggle", tr("Brush"), tr("Brush &Settings"),       QKeySequence(Qt::Key_F5),                  ShortcutScope::GlobalSafe);

    // ── Panels ──
    add("panel.layers.toggle",  tr("Panels"), tr("Toggle &Layers Panel"), QKeySequence(Qt::Key_F7), ShortcutScope::GlobalSafe);
    add("panel.layers.focus",   tr("Panels"), tr("Focus Layers Panel"),   {},                       ShortcutScope::GlobalSafe);

    // ── View ──
    add("view.zoom_in",         tr("View"), tr("Zoom &In"),                QKeySequence::ZoomIn,                    ShortcutScope::Canvas,
        { QKeySequence(Qt::CTRL | Qt::Key_Equal) });
    add("view.zoom_out",        tr("View"), tr("Zoom &Out"),               QKeySequence::ZoomOut,                   ShortcutScope::Canvas);
    add("view.zoom_fit",        tr("View"), tr("&Fit to Screen"),          QKeySequence(Qt::CTRL | Qt::Key_0),      ShortcutScope::Canvas);
    add("view.actual_size",     tr("View"), tr("&Actual Size"),            QKeySequence(Qt::CTRL | Qt::Key_1),      ShortcutScope::Canvas);
    add("view.reset",           tr("View"), tr("&Reset View"),             {});
    add("view.show_rulers",     tr("View"), tr("Show &Rulers"),            QKeySequence(Qt::CTRL | Qt::Key_R));
    add("view.show_guides",     tr("View"), tr("Show &Guides"),            QKeySequence(Qt::CTRL | Qt::Key_Semicolon));
    add("view.lock_guides",     tr("View"), tr("&Lock Guides"),            {});
    add("view.clear_guides",    tr("View"), tr("&Clear Guides"),           {});
    add("view.enable_snap",     tr("View"), tr("&Enable Snap"),            QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Semicolon));
    add("view.snap_to_canvas",  tr("View"), tr("Snap to &Canvas"),         {});
    add("view.snap_to_guides",  tr("View"), tr("Snap to &Guides"),         {});
    add("view.snap_settings",   tr("View"), tr("Snap &Settings..."),       {});

    // ── Canvas (view toggles, non-tool keys) ──
    add("canvas.quick_mask",    tr("Canvas"), tr("Toggle &Quick Mask"),    QKeySequence(Qt::Key_Q),         ShortcutScope::Canvas);
    // Canvas scope (not MaskEdit): "\" toggles the rubylith whenever the active
    // layer has a mask, matching the canvas behaviour it replaces. The has-mask
    // gate lives in CanvasView::toggleMaskRubylith().
    add("canvas.edit_mask",     tr("Canvas"), tr("Toggle Mask &Overlay"), QKeySequence(Qt::Key_Backslash), ShortcutScope::Canvas);
    add("canvas.cancel",        tr("Canvas"), tr("&Cancel Operation"),     QKeySequence(Qt::Key_Escape),    ShortcutScope::Modal);
    add("canvas.commit",        tr("Canvas"), tr("&Commit Operation"),     QKeySequence(Qt::Key_Return),    ShortcutScope::Modal,
        { QKeySequence(Qt::Key_Enter) });
    add("canvas.delete_layer",  tr("Canvas"), tr("&Delete Layer/Selection"), QKeySequence(Qt::Key_Backspace), ShortcutScope::Canvas);

    // ── Crop modal ── Modal scope: only fire while the Crop
    // tool's crop session is active (context.modalActive), so O/X don't collide
    // with the global meanings elsewhere.
    add("tool.crop.cycle_guides",            tr("Crop"), tr("Cycle Crop &Guides"),          QKeySequence(Qt::Key_O), ShortcutScope::Modal);
    add("tool.crop.swap_aspect_orientation", tr("Crop"), tr("Swap Crop &Orientation"),      QKeySequence(Qt::Key_X), ShortcutScope::Modal);

    // ── Color ──
    // color.swap is Canvas-scoped (not GlobalSafe) so typing 'x' in a canvas text
    // edit stays text, and so the Crop modal's X (swap orientation) outranks it.
    add("color.swap",           tr("Color"), tr("Swap &Foreground/Background"), QKeySequence(Qt::Key_X), ShortcutScope::Canvas);
    add("color.reset_default",  tr("Color"), tr("&Reset Default Colors"),      QKeySequence(Qt::Key_D), ShortcutScope::GlobalSafe);

    // Compatibility aliases for the shortcut refactor spec.
    registerAlias(QStringLiteral("selection.select_all"), QStringLiteral("select.all"));
    registerAlias(QStringLiteral("selection.deselect"), QStringLiteral("select.deselect"));
    registerAlias(QStringLiteral("selection.invert"), QStringLiteral("select.invert"));
    registerAlias(QStringLiteral("selection.refine"), QStringLiteral("select.refine"));
    registerAlias(QStringLiteral("selection.modify.feather"), QStringLiteral("select.feather"));
    registerAlias(QStringLiteral("selection.modify.grow"), QStringLiteral("select.grow"));
    registerAlias(QStringLiteral("selection.modify.shrink"), QStringLiteral("select.shrink"));
    registerAlias(QStringLiteral("selection.modify.border"), QStringLiteral("select.border"));
    registerAlias(QStringLiteral("selection.modify.smooth"), QStringLiteral("select.smooth"));
    registerAlias(QStringLiteral("view.fit_to_viewport"), QStringLiteral("view.zoom_fit"));
    registerAlias(QStringLiteral("view.zoom_100"), QStringLiteral("view.actual_size"));
    registerAlias(QStringLiteral("layer.new.dialog"), QStringLiteral("layer.new"));
    registerAlias(QStringLiteral("layer.delete"), QStringLiteral("layer.remove"));
    registerAlias(QStringLiteral("layer.merge.visible"), QStringLiteral("layer.merge_visible"));
    registerAlias(QStringLiteral("layer.merge.down"), QStringLiteral("layer.merge_down"));
    registerAlias(QStringLiteral("layer.mask.add_reveal_all"), QStringLiteral("mask.add"));
    registerAlias(QStringLiteral("layer.mask.remove"), QStringLiteral("mask.remove"));
    registerAlias(QStringLiteral("layer.mask.apply"), QStringLiteral("mask.apply"));
    registerAlias(QStringLiteral("layer.mask.enable_toggle"), QStringLiteral("mask.toggle"));
    registerAlias(QStringLiteral("layer.mask.from_selection"), QStringLiteral("mask.from_selection"));
    registerAlias(QStringLiteral("layer.mask.invert"), QStringLiteral("mask.invert"));
    registerAlias(QStringLiteral("layer.mask.copy"), QStringLiteral("mask.copy"));
    registerAlias(QStringLiteral("layer.mask.paste"), QStringLiteral("mask.paste"));
    registerAlias(QStringLiteral("layer.mask.clear"), QStringLiteral("mask.clear"));
    registerAlias(QStringLiteral("layer.mask.overlay_toggle"), QStringLiteral("canvas.edit_mask"));
    registerAlias(QStringLiteral("layer.adjustment.clip_toggle"), QStringLiteral("layer.clipping.toggle"));
}

QStringList ShortcutManager::categories() const
{
    QStringList cats;
    for (const auto& a : m_actions) {
        if (!cats.contains(a.category))
            cats.append(a.category);
    }
    return cats;
}

QVector<ShortcutManager::ActionDef> ShortcutManager::actionsInCategory(const QString& category) const
{
    QVector<ActionDef> result;
    for (const auto& a : m_actions) {
        if (a.category == category)
            result.append(a);
    }
    return result;
}

bool ShortcutManager::hasAction(const QString& id) const
{
    return m_actionIndex.contains(canonicalId(id));
}

QString ShortcutManager::canonicalId(const QString& id) const
{
    QString current = id;
    QSet<QString> seen;
    while (m_aliases.contains(current) && !seen.contains(current)) {
        seen.insert(current);
        current = m_aliases.value(current);
    }
    return current;
}

ShortcutManager::ActionDef ShortcutManager::actionDefinition(const QString& id) const
{
    const QString key = canonicalId(id);
    auto it = m_actionIndex.find(key);
    if (it != m_actionIndex.end())
        return m_actions[it.value()];
    return {};
}

ShortcutManager::ShortcutScope ShortcutManager::actionScope(const QString& id) const
{
    return actionDefinition(id).scope;
}

QString ShortcutManager::actionLabel(const QString& id) const
{
    return actionDefinition(id).label;
}

QKeySequence ShortcutManager::currentShortcut(const QString& id) const
{
    const QString key = canonicalId(id);
    if (m_custom.contains(key))
        return m_custom.value(key);
    auto it = m_actionIndex.find(key);
    if (it != m_actionIndex.end())
        return m_actions[it.value()].defaultSeq;
    return {};
}

QVector<QKeySequence> ShortcutManager::currentShortcuts(const QString& id) const
{
    const QString key = canonicalId(id);
    if (m_custom.contains(key)) {
        const QKeySequence seq = m_custom.value(key);
        return seq.isEmpty() ? QVector<QKeySequence>{} : QVector<QKeySequence>{seq};
    }
    return defaultShortcuts(key);
}

QKeySequence ShortcutManager::defaultShortcut(const QString& id) const
{
    auto it = m_actionIndex.find(canonicalId(id));
    if (it != m_actionIndex.end())
        return m_actions[it.value()].defaultSeq;
    return {};
}

QVector<QKeySequence> ShortcutManager::defaultShortcuts(const QString& id) const
{
    QVector<QKeySequence> seqs;
    auto it = m_actionIndex.find(canonicalId(id));
    if (it == m_actionIndex.end())
        return seqs;

    const auto& action = m_actions[it.value()];
    if (!action.defaultSeq.isEmpty())
        seqs.append(action.defaultSeq);
    for (const auto& seq : action.defaultAlternatives) {
        if (!seq.isEmpty() && !seqs.contains(seq))
            seqs.append(seq);
    }
    return seqs;
}

bool ShortcutManager::isModified(const QString& id) const
{
    return m_custom.contains(canonicalId(id));
}

QString ShortcutManager::shortcutDisplayText(const QString& id) const
{
    const auto seqs = currentShortcuts(id);
    if (seqs.isEmpty())
        return QStringLiteral("\u2014");

    QStringList parts;
    for (const auto& seq : seqs)
        parts.append(seq.toString(QKeySequence::NativeText));
    return parts.join(QStringLiteral(" / "));
}

QString ShortcutManager::shortcutTooltipText(const QString& id, const QString& baseText) const
{
    const QString label = baseText.isEmpty() ? actionLabel(id) : baseText;
    const auto seqs = currentShortcuts(id);
    if (label.isEmpty() || seqs.isEmpty())
        return label;
    return label + QStringLiteral(" (") + shortcutDisplayText(id) + QStringLiteral(")");
}

bool ShortcutManager::setShortcut(const QString& id, const QKeySequence& seq, bool silent)
{
    const QString key = canonicalId(id);
    if (!m_actionIndex.contains(key))
        return false;

    // Check for conflicts (skip if silent or seq is empty)
    if (!seq.isEmpty() && !silent) {
        QString conflict = findConflict(seq, key);
        if (!conflict.isEmpty())
            return false;
    }

    QKeySequence def = defaultShortcut(key);
    if (seq == def || (seq.isEmpty() && def.isEmpty())) {
        m_custom.remove(key);
    } else {
        m_custom[key] = seq;
    }

    applyShortcutBindings(key);
    saveSettings();
    emit shortcutsChanged();
    return true;
}

void ShortcutManager::resetToDefault(const QString& id)
{
    const QString key = canonicalId(id);
    m_custom.remove(key);
    applyShortcutBindings(key);

    saveSettings();
    emit shortcutsChanged();
}

void ShortcutManager::resetAll()
{
    m_custom.clear();
    for (auto it = m_boundActions.begin(); it != m_boundActions.end(); ++it)
        applyShortcutBindings(it.key());
    for (auto it = m_boundShortcuts.begin(); it != m_boundShortcuts.end(); ++it)
        applyShortcutBindings(it.key());
    saveSettings();
    emit shortcutsChanged();
}

QString ShortcutManager::findConflict(const QKeySequence& seq, const QString& exceptId) const
{
    if (seq.isEmpty()) return {};
    const QString exceptKey = canonicalId(exceptId);
    for (const auto& a : m_actions) {
        if (canonicalId(a.id) == exceptKey) continue;
        for (const auto& cur : currentShortcuts(a.id)) {
            if (!cur.isEmpty() && cur == seq)
                return a.label;
        }
    }
    return {};
}

QString ShortcutManager::findConflict(const QKeySequence& seq, const QString& exceptId,
                                      const DispatchContext& context) const
{
    if (seq.isEmpty()) return {};
    const QString exceptKey = canonicalId(exceptId);
    for (const auto& a : m_actions) {
        if (canonicalId(a.id) == exceptKey) continue;
        if (!actionEnabledInContext(a.id, context)) continue;
        for (const auto& cur : currentShortcuts(a.id)) {
            if (!cur.isEmpty() && cur == seq)
                return a.label;
        }
    }
    return {};
}

bool ShortcutManager::matchesShortcut(const QString& id, const QKeySequence& seq) const
{
    if (seq.isEmpty())
        return false;
    for (const auto& cur : currentShortcuts(id)) {
        if (!cur.isEmpty() && cur == seq)
            return true;
    }
    return false;
}

bool ShortcutManager::actionEnabledInContext(const QString& id,
                                             const DispatchContext& context) const
{
    const QString key = canonicalId(id);
    auto it = m_actionIndex.find(key);
    if (it == m_actionIndex.end())
        return false;
    return contextPriorityForScope(m_actions[it.value()].scope, context) >= 0;
}

QString ShortcutManager::actionForSequence(const QKeySequence& seq,
                                           const DispatchContext& context) const
{
    if (seq.isEmpty())
        return {};

    QString bestId;
    int bestPriority = -1;
    for (const auto& action : m_actions) {
        if (!matchesShortcut(action.id, seq))
            continue;

        const int priority = contextPriorityForScope(action.scope, context);
        if (priority > bestPriority) {
            bestPriority = priority;
            bestId = action.id;
        }
    }
    return bestPriority >= 0 ? bestId : QString();
}

QStringList ShortcutManager::actionsForSequence(const QKeySequence& seq,
                                                const DispatchContext& context) const
{
    QStringList ids;
    if (seq.isEmpty())
        return ids;

    for (const auto& action : m_actions) {
        if (matchesShortcut(action.id, seq)
            && contextPriorityForScope(action.scope, context) >= 0) {
            ids.append(action.id);
        }
    }
    return ids;
}

bool ShortcutManager::dispatchShortcut(const QKeySequence& seq, const DispatchContext& context)
{
    const QString id = canonicalId(actionForSequence(seq, context));
    if (id.isEmpty())
        return false;

    auto it = m_callbacks.find(id);
    if (it == m_callbacks.end())
        return false;

    it.value()();
    return true;
}

bool ShortcutManager::trigger(const QString& id)
{
    auto it = m_callbacks.find(canonicalId(id));
    if (it == m_callbacks.end())
        return false;
    it.value()();
    return true;
}

QKeySequence ShortcutManager::eventSequence(const QKeyEvent* event)
{
    if (!event)
        return {};
    return QKeySequence(event->modifiers() | event->key());
}

void ShortcutManager::registerAlias(const QString& aliasId, const QString& targetId)
{
    if (aliasId.isEmpty() || targetId.isEmpty() || aliasId == targetId)
        return;
    m_aliases[aliasId] = targetId;
}

void ShortcutManager::registerAction(const QString& id, QAction* action)
{
    const QString key = canonicalId(id);
    m_boundActions[key] = action;

    // Connect to shortcutsChanged for live updates
    if (m_connections.contains(key))
        disconnect(m_connections.value(key));
    m_connections[key] = connect(this, &ShortcutManager::shortcutsChanged, action, [this, action, key]() {
        action->setShortcuts(toShortcutList(currentShortcuts(key)));
    });
    applyShortcutBindings(key);
}

void ShortcutManager::registerShortcut(const QString& id, QShortcut* sc)
{
    const QString key = canonicalId(id);
    m_boundShortcuts[key] = sc;
    applyShortcutBindings(key);
}

void ShortcutManager::registerCallback(const QString& id, std::function<void()> callback)
{
    m_callbacks[canonicalId(id)] = std::move(callback);
}

void ShortcutManager::loadSettings()
{
    QSettings settings;
    int count = settings.beginReadArray("custom");
    for (int i = 0; i < count; ++i) {
        settings.setArrayIndex(i);
        QString id = settings.value("id").toString();
        QString seqStr = settings.value("seq").toString();
        const QString key = canonicalId(id);
        if (!m_actionIndex.contains(key))
            continue;
        if (seqStr == kClearedShortcutMarker || seqStr.isEmpty()) {
            m_custom[key] = QKeySequence();
        } else {
            m_custom[key] = QKeySequence::fromString(seqStr, QKeySequence::PortableText);
        }
    }
    settings.endArray();
}

void ShortcutManager::saveSettings()
{
    QSettings settings;
    settings.beginWriteArray("custom");
    int idx = 0;
    for (auto it = m_custom.begin(); it != m_custom.end(); ++it) {
        settings.setArrayIndex(idx++);
        settings.setValue("id", it.key());
        settings.setValue("seq", it.value().isEmpty()
            ? kClearedShortcutMarker
            : it.value().toString(QKeySequence::PortableText));
    }
    settings.endArray();
}

void ShortcutManager::applyShortcutBindings(const QString& id)
{
    const QString key = canonicalId(id);
    const QVector<QKeySequence> seqs = currentShortcuts(key);

    if (auto* action = m_boundActions.value(key))
        action->setShortcuts(toShortcutList(seqs));

    if (auto* sc = m_boundShortcuts.value(key))
        sc->setKey(seqs.isEmpty() ? QKeySequence() : seqs.first());
}

int ShortcutManager::contextPriorityForScope(ShortcutScope scope,
                                             const DispatchContext& context) const
{
    const bool textWidgetActive = context.textInputFocused
                               || context.inlineRenameActive
                               || context.popupActive;

    if (textWidgetActive)
        return scope == ShortcutScope::TextEditing ? 700 : -1;

    if (context.textCanvasEditingActive) {
        switch (scope) {
        case ShortcutScope::TextEditing: return 700;
        case ShortcutScope::GlobalSafe:  return 100;
        case ShortcutScope::Global:      return 50;
        default:                         return -1;
        }
    }

    if (context.modalActive) {
        switch (scope) {
        case ShortcutScope::Modal:      return 650;
        case ShortcutScope::GlobalSafe: return 100;
        case ShortcutScope::Global:     return 50;
        default:                        return -1;
        }
    }

    switch (scope) {
    case ShortcutScope::TextEditing:
        return -1;
    case ShortcutScope::MaskTarget:
        return context.maskTargetActive ? 550 : -1;
    case ShortcutScope::MaskEdit:
        return context.maskEditActive ? 540 : -1;
    case ShortcutScope::LayerPanel:
        return context.layerPanelFocused ? 500 : -1;
    case ShortcutScope::LayerPanelOrMoveTool:
        if (context.layerPanelFocused) return 500;
        return context.moveToolActive ? 360 : -1;
    case ShortcutScope::LayerPanelOrCanvas:
        if (context.layerPanelFocused) return 500;
        return context.canvasFocused ? 320 : -1;
    case ShortcutScope::ToolOption:
        return context.toolActive ? 350 : -1;
    case ShortcutScope::Tool:
        return (context.toolActive || context.canvasFocused) ? 300 : -1;
    case ShortcutScope::Canvas:
        return context.canvasFocused ? 250 : -1;
    case ShortcutScope::GlobalSafe:
        return 100;
    case ShortcutScope::Global:
        return 50;
    }

    return -1;
}

QList<QKeySequence> ShortcutManager::toShortcutList(const QVector<QKeySequence>& seqs)
{
    QList<QKeySequence> result;
    for (const auto& seq : seqs) {
        if (!seq.isEmpty() && !result.contains(seq))
            result.append(seq);
    }
    return result;
}
