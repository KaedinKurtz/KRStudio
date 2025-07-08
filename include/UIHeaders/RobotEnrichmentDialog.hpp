#pragma once

#include "RobotDescription.hpp"
#include <QDialog>
#include <memory> // Required for std::unique_ptr

// Forward declare UI elements to keep headers clean
class PreviewViewport;
class QTreeWidget;
class QTreeWidgetItem;
class QStackedWidget;
class LinkPropertiesWidget;
class JointPropertiesWidget;

class RobotEnrichmentDialog : public QDialog
{
    Q_OBJECT

public:
    explicit RobotEnrichmentDialog(const RobotDescription& robotDesc, QWidget* parent = nullptr);
    ~RobotEnrichmentDialog() override;

    // This function returns the final, enriched description to the MainWindow.
    const RobotDescription& getFinalDescription() const;

private slots:
    // This slot is called when the user clicks a different item in the tree.
    void onCurrentTreeItemChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous);

    // This slot is called whenever a property in an editor widget changes.
    void onPropertiesChanged();

    // This slot is connected to the "Save" button to ensure data is updated before closing.
    void onSave();

private:
    void populateTree();
    void createPropertyEditors();

    // The dialog now owns a modifiable copy of the robot description via a smart pointer.
    std::unique_ptr<RobotDescription> m_robotDescription;

    // UI Elements
    PreviewViewport* m_previewViewport;
    QTreeWidget* m_robotTreeWidget;
    QStackedWidget* m_propertyEditorStack;

    // Property Editor Widgets
    LinkPropertiesWidget* m_linkEditor;
    JointPropertiesWidget* m_jointEditor;
    QWidget* m_defaultEditor; // A blank widget for when nothing is selected
};
