#pragma once

#include <QWidget>
#include <QColor>

class QLineEdit;
class QGridLayout;

class ColorFieldsWidget : public QWidget {
    Q_OBJECT
public:
    explicit ColorFieldsWidget(QWidget* parent = nullptr);

    void setColor(const QColor& color);

signals:
    void colorChanged(const QColor& color);

private slots:
    void onHsvChanged();
    void onRgbChanged();
    void onHexChanged();
    void onCmykChanged();

private:
    void setupUi();
    void updateFieldsFromColor(const QColor& color);
    void maybeMarkEditable();

    QColor currentColor() const;

    void computeLab(const QColor& color, double& L, double& a, double& b) const;

    bool m_updatingFields = false;

    QLineEdit* m_hueEdit = nullptr;
    QLineEdit* m_satEdit = nullptr;
    QLineEdit* m_briEdit = nullptr;

    QLineEdit* m_rEdit = nullptr;
    QLineEdit* m_gEdit = nullptr;
    QLineEdit* m_bEdit = nullptr;

    QLineEdit* m_hexEdit = nullptr;

    QLineEdit* m_lEdit = nullptr;
    QLineEdit* m_aEdit = nullptr;
    QLineEdit* m_bLabEdit = nullptr;

    QLineEdit* m_cEdit = nullptr;
    QLineEdit* m_mEdit = nullptr;
    QLineEdit* m_yEdit = nullptr;
    QLineEdit* m_kEdit = nullptr;
};
