#pragma once

#include <QWidget>

class QVBoxLayout;

class PopupPanel : public QWidget {
    Q_OBJECT

public:
    explicit PopupPanel(QWidget* parent = nullptr);

    void setContentWidget(QWidget* content);
    void showAnchoredTo(QWidget* anchor);
    void toggleAnchoredTo(QWidget* anchor);

protected:
    void keyPressEvent(QKeyEvent* event) override;

private:
    void applyTheme();

    QVBoxLayout* m_layout = nullptr;
    QWidget* m_content = nullptr;
};
