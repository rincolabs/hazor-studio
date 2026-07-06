#pragma once

#include <QWidget>

class QTreeWidget;
class QTreeWidgetItem;
class QLineEdit;
class QLabel;
class QPushButton;

class ShortcutSettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit ShortcutSettingsPage(QWidget* parent = nullptr);

    void load();
    void apply();

signals:
    void shortcutsChanged();

private slots:
    void onSearchTextChanged(const QString& text);
    void onTreeItemClicked(QTreeWidgetItem* item, int column);
    void onResetAll();
    void onResetSelected();

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void applyTheme();
    void buildTree();
    void populateItem(QTreeWidgetItem* item, const QString& id, const QString& label);
    void startCapture(QTreeWidgetItem* item);
    void finishCapture(const QKeySequence& seq);
    void cancelCapture();
    QString shortcutText(const QKeySequence& seq) const;
    void updateWarning(const QString& text);

    QLineEdit* m_searchEdit = nullptr;
    QPushButton* m_resetAllBtn = nullptr;
    QTreeWidget* m_tree = nullptr;
    QLabel* m_warningLabel = nullptr;

    bool m_capturing = false;
    QTreeWidgetItem* m_capturingItem = nullptr;
    QString m_capturingId;
    QString m_warningText;
    QKeySequence m_captureBefore;
    QKeySequence m_capturePreserved;
};
