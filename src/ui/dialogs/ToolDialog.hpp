#pragma once

#include <QDialog>
#include <QVariantMap>

class QCheckBox;
class QPushButton;

class ToolDialog : public QDialog {
    Q_OBJECT
public:
    enum class Mode { Direct, Adjustment };

    ToolDialog(const QString& title, Mode mode, QWidget* parent = nullptr);

    Mode mode() const { return m_mode; }

    virtual QVariantMap collectParams() const = 0;

signals:
    void previewChanged(const QVariantMap& params);
    void confirmed(const QVariantMap& params);
    void cancelled();

protected:
    void setDefaults(const QVariantMap& defaults);
    QVariantMap defaults() const { return m_defaults; }

    void markParamChanged();
    virtual void resetToDefaults();

    QCheckBox* m_previewToggle = nullptr;

private slots:
    void onApply();
    void onCancel();
    void onReset();

private:
    QPushButton* m_resetBtn = nullptr;
    QPushButton* m_applyBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QVariantMap m_defaults;
    Mode m_mode;
};
