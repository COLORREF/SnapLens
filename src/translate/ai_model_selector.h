#pragma once

#include "ai_request_options.h"
#include <QHash>
#include <QWidget>

class QComboBox;
class QLabel;
class Settings;

/** Shared quick controls for text and screenshot translation.
 * Selections are local to this workspace, keyed by endpoint/model, and never save
 * global settings. snapshot() loads the selected model's full profile before
 * applying explicit quick overrides, so unrelated models cannot share parameters.
 */
class AiModelSelector : public QWidget {
    Q_OBJECT
public:
    explicit AiModelSelector(Settings* settings, QWidget* parent = nullptr);
    void refresh();
    AiRequestOptions snapshot();

protected:
    void showEvent(QShowEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    struct Choice { QString thinking = "profile"; QString effort = "profile"; };
    void refreshChoices();
    void rememberChoice();
    QString effortSelection() const;
    Settings* m_settings;
    QComboBox* m_model;
    QComboBox* m_thinking;
    QComboBox* m_effort;
    QLabel* m_hint;
    QString m_defaults;
    QString m_activeModel;
    QHash<QString, Choice> m_choices;
    bool m_refreshing = false;
};
