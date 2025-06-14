#include "URDFImporterDialog.hpp"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QPushButton>
#include <QHeaderView>

URDFImporterDialog::URDFImporterDialog(const RobotDescription& robotDesc, QWidget* parent)
    : QDialog(parent), m_robotDescription(robotDesc)
{
    setWindowTitle("URDF Import & Verification");
    setMinimumSize(600, 700);

    // --- Main Layout ---
    QVBoxLayout* mainLayout = new QVBoxLayout(this);

    // --- Tree Widget ---
    m_treeWidget = new QTreeWidget(this);
    m_treeWidget->setColumnCount(2);
    m_treeWidget->setHeaderLabels({ "Name", "Type" });
    m_treeWidget->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    mainLayout->addWidget(m_treeWidget);

    // --- Buttons Layout ---
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    m_importButton = new QPushButton("Verify and Import", this);
    m_cancelButton = new QPushButton("Cancel", this);
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_cancelButton);
    buttonLayout->addWidget(m_importButton);
    mainLayout->addLayout(buttonLayout);

    // --- Populate the data ---
    populateTreeWidget();
    m_treeWidget->expandAll();

    // --- Connections ---
    connect(m_importButton, &QPushButton::clicked, this, &URDFImporterDialog::onVerifyAndImport);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
}

void URDFImporterDialog::populateTreeWidget()
{
    m_treeWidget->clear();

    QTreeWidgetItem* robotItem = new QTreeWidgetItem(m_treeWidget);
    robotItem->setText(0, QString::fromStdString(m_robotDescription.name));
    robotItem->setText(1, "Robot");
    robotItem->setIcon(0, QIcon(":/icons/robot.png")); // Assuming you add a robot icon to your resources

    QTreeWidgetItem* linksRootItem = new QTreeWidgetItem(robotItem);
    linksRootItem->setText(0, "Links");
    linksRootItem->setIcon(0, QIcon(":/icons/folder.png")); // Assuming a folder icon

    for (const auto& link : m_robotDescription.links)
    {
        QTreeWidgetItem* linkItem = new QTreeWidgetItem(linksRootItem);
        linkItem->setText(0, QString::fromStdString(link.name));
        linkItem->setText(1, "Link");
    }

    QTreeWidgetItem* jointsRootItem = new QTreeWidgetItem(robotItem);
    jointsRootItem->setText(0, "Joints");
    jointsRootItem->setIcon(0, QIcon(":/icons/folder.png"));

    for (const auto& joint : m_robotDescription.joints)
    {
        QTreeWidgetItem* jointItem = new QTreeWidgetItem(jointsRootItem);
        jointItem->setText(0, QString::fromStdString(joint.name));
        jointItem->setText(1, "Joint");
    }
}

void URDFImporterDialog::onVerifyAndImport()
{
    // In a real application, you would do validation here.
    // For now, we just accept the result.
    // This will close the dialog and return QDialog::Accepted.
    accept();
}
