#pragma once

#include "RobotDescription.hpp"
#include <QDialog>

class QTreeWidget;
class QTreeWidgetItem;
class QPushButton;

// A dialog window for importing a URDF, viewing its structure,
// and confirming the import process.
class URDFImporterDialog : public QDialog
{
    Q_OBJECT

public:
    // The constructor takes the parsed robot data and a parent widget.
    explicit URDFImporterDialog(const RobotDescription& robotDesc, QWidget* parent = nullptr);

    // A public getter to retrieve the (potentially modified) robot description.
    const RobotDescription& getRobotDescription() const { return m_robotDescription; }

private slots:
    void onVerifyAndImport();

private:
    void populateTreeWidget();

    RobotDescription m_robotDescription; // A local copy of the data to be modified.

    // UI Elements
    QTreeWidget* m_treeWidget;
    QPushButton* m_importButton;
    QPushButton* m_cancelButton;
};
