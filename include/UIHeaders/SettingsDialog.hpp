#pragma once

#include <QDialog>
#include <QHash>
#include <QString>
#include <functional>

class QListWidget;
class QStackedWidget;

/**
 * @brief Registry-driven Settings dialog. Iterates SettingsManager's registry to
 * build one category page per category with the appropriate editor per setting
 * type. Edits write straight to SettingsManager (persisted + hot-swapped via the
 * applier map in MainWindow); the engine render loop shows visual changes live.
 * Modeless so changes can be seen on the viewport while the dialog stays open.
 */
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private:
    void buildUi();
    QWidget* buildCategoryPage(const QString& category);

    QListWidget*    m_categoryList = nullptr;
    QStackedWidget* m_stack = nullptr;
    bool            m_updating = false; // guard so programmatic re-sync doesn't re-emit

    // key -> re-read the persisted value into its editor (used on Reset / external change)
    QHash<QString, std::function<void()>> m_syncers;
};
