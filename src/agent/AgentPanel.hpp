#pragma once

#include <QWidget>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QComboBox>
#include <QVBoxLayout>
#include <QTextEdit>

class AgentController;

class AgentPanel : public QWidget {
    Q_OBJECT

public:
    explicit AgentPanel(AgentController* controller, QWidget* parent = nullptr);

    void setAgentList(const QStringList& names);
    void setActiveAgent(const QString& name);
    void setModelInfo(const QString& info);

signals:
    void inputSubmitted(const QString& text);
    void agentSelected(const QString& name);
    void configureClicked();

private slots:
    void onSubmit();
    void onProcessingStarted();
    void onProcessingFinished();
    void onToolExecuted(const QString& name, bool success);
    void onError(const QString& msg);
    void onAgentComboChanged(int index);

private:
    AgentController* m_controller = nullptr;

    QComboBox* m_agentCombo = nullptr;
    QLabel* m_modelInfo = nullptr;
    QPushButton* m_configBtn = nullptr;
    QTextEdit* m_logView = nullptr;
    QLineEdit* m_input = nullptr;
    QPushButton* m_submitBtn = nullptr;
    QLabel* m_statusLabel = nullptr;

    // Tracks whether assistant text was shown since the last turn started, so
    // we can surface a placeholder when the model replies with tool-calls only.
    bool m_assistantTextShown = false;
};
