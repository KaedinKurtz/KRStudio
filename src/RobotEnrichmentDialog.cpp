#include "RobotEnrichmentDialog.hpp"
#include "PreviewViewport.hpp"
#include "LinkPropertiesWidget.hpp"
#include "JointPropertiesWidget.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QTreeWidget>
#include <QStackedWidget>
#include <QHeaderView>
#include <QLabel>
#include <QDialogButtonBox>
#include <QSlider>
#include <QDebug>
#include <algorithm> // For std::find_if

// A helper to store data in the tree item without making it visible to the user.
const int ItemTypeRole = Qt::UserRole; // Stores an ItemType enum value.
const int ItemNameRole = Qt::UserRole + 1; // Stores the name (as a QString) of the link/joint.

// This enum must be defined here or in a shared header so both the .hpp and .cpp can see it.
enum class ItemType { Robot, Link, Joint, Root };

RobotEnrichmentDialog::RobotEnrichmentDialog(const RobotDescription& robotDesc, QWidget* parent)
    : QDialog(parent), m_robotDescription(std::make_unique<RobotDescription>(robotDesc))
{
    setWindowTitle("Robot Import & Enrichment");
    setMinimumSize(1200, 800);

    // --- Main Layout ---
    auto* mainLayout = new QVBoxLayout(this);
    auto* splitter = new QSplitter(Qt::Horizontal, this);
    mainLayout->addWidget(splitter, 1);

    // --- Left Pane (Tree & Property Editors) ---
    auto* leftPane = new QWidget(splitter);
    auto* leftLayout = new QVBoxLayout(leftPane);
    m_robotTreeWidget = new QTreeWidget(this);
    m_propertyEditorStack = new QStackedWidget(this);
    leftLayout->addWidget(m_robotTreeWidget, 1);
    leftLayout->addWidget(m_propertyEditorStack, 2);
    leftPane->setLayout(leftLayout);

    // --- Right Pane (Preview & Controls) ---
    auto* rightPane = new QWidget(splitter);
    auto* rightLayout = new QVBoxLayout(rightPane);
    m_previewViewport = new PreviewViewport(this);

    auto* speedSlider = new QSlider(Qt::Horizontal, this);
    speedSlider->setRange(0, 100);
    speedSlider->setValue(50);
    auto* speedLayout = new QHBoxLayout();
    speedLayout->addWidget(new QLabel("Anim Speed:"));
    speedLayout->addWidget(speedSlider);

    rightLayout->addWidget(m_previewViewport, 1);
    rightLayout->addLayout(speedLayout);
    rightPane->setLayout(rightLayout);

    splitter->addWidget(leftPane);
    splitter->addWidget(rightPane);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 2);

    auto* buttonBox = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, this);
    mainLayout->addWidget(buttonBox);

    createPropertyEditors();
    populateTree();
    m_robotTreeWidget->expandAll();

    m_previewViewport->loadRobotForPreview(*m_robotDescription);
    m_propertyEditorStack->setCurrentWidget(m_defaultEditor);

    // --- Connections ---
    connect(m_robotTreeWidget, &QTreeWidget::currentItemChanged, this, &RobotEnrichmentDialog::onCurrentTreeItemChanged);
    connect(m_linkEditor, &LinkPropertiesWidget::propertiesChanged, this, &RobotEnrichmentDialog::onPropertiesChanged);

    connect(buttonBox, &QDialogButtonBox::accepted, this, &RobotEnrichmentDialog::onSave);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

RobotEnrichmentDialog::~RobotEnrichmentDialog() = default;

// This getter returns a const reference to the object managed by the unique_ptr.
const RobotDescription& RobotEnrichmentDialog::getFinalDescription() const
{
    return *m_robotDescription;
}

void RobotEnrichmentDialog::createPropertyEditors()
{
    m_defaultEditor = new QWidget();
    m_defaultEditor->setLayout(new QVBoxLayout());
    auto* defaultLabel = new QLabel("Select a link or joint from the tree to edit its properties.", m_defaultEditor);
    defaultLabel->setAlignment(Qt::AlignCenter);
    defaultLabel->setWordWrap(true);
    m_defaultEditor->layout()->addWidget(defaultLabel);

    m_linkEditor = new LinkPropertiesWidget(this);
    m_jointEditor = new JointPropertiesWidget(this);

    m_propertyEditorStack->addWidget(m_defaultEditor);
    m_propertyEditorStack->addWidget(m_linkEditor);
    m_propertyEditorStack->addWidget(m_jointEditor);
}

void RobotEnrichmentDialog::populateTree()
{
    m_robotTreeWidget->clear();

    // Use -> to access members of the object pointed to by m_robotDescription
    auto* robotItem = new QTreeWidgetItem(m_robotTreeWidget, { QString::fromStdString(m_robotDescription->name) });
    robotItem->setData(0, ItemTypeRole, QVariant::fromValue(static_cast<int>(ItemType::Robot)));

    auto* linksRoot = new QTreeWidgetItem(robotItem, { "Links" });
    linksRoot->setData(0, ItemTypeRole, QVariant::fromValue(static_cast<int>(ItemType::Root)));

    for (const auto& link : m_robotDescription->links) {
        auto* linkItem = new QTreeWidgetItem(linksRoot, { QString::fromStdString(link.name) });
        linkItem->setData(0, ItemTypeRole, QVariant::fromValue(static_cast<int>(ItemType::Link)));
        linkItem->setData(0, ItemNameRole, QVariant::fromValue(QString::fromStdString(link.name)));
    }

    auto* jointsRoot = new QTreeWidgetItem(robotItem, { "Joints" });
    jointsRoot->setData(0, ItemTypeRole, QVariant::fromValue(static_cast<int>(ItemType::Root)));

    for (const auto& joint : m_robotDescription->joints) {
        auto* jointItem = new QTreeWidgetItem(jointsRoot, { QString::fromStdString(joint.name) });
        jointItem->setData(0, ItemTypeRole, QVariant::fromValue(static_cast<int>(ItemType::Joint)));
        jointItem->setData(0, ItemNameRole, QVariant::fromValue(QString::fromStdString(joint.name)));
    }
}

void RobotEnrichmentDialog::onCurrentTreeItemChanged(QTreeWidgetItem* current, QTreeWidgetItem* previous)
{
    if (!current) {
        m_propertyEditorStack->setCurrentWidget(m_defaultEditor);
        return;
    }

    ItemType type = static_cast<ItemType>(current->data(0, ItemTypeRole).toInt());
    std::string name = current->data(0, ItemNameRole).toString().toStdString();

    switch (type)
    {
    case ItemType::Link: {
        // Find the link in our description struct by name.
        auto it = std::find_if(m_robotDescription->links.begin(), m_robotDescription->links.end(),
            [&](LinkDescription& link) { return link.name == name; });
        if (it != m_robotDescription->links.end()) {
            m_linkEditor->setLink(&(*it)); // Pass a pointer to the found link.
            m_propertyEditorStack->setCurrentWidget(m_linkEditor);
        }
        break;
    }
    case ItemType::Joint: {
        // TODO: Implement the same logic for joints.
        m_propertyEditorStack->setCurrentWidget(m_jointEditor);
        break;
    }
    default:
        m_propertyEditorStack->setCurrentWidget(m_defaultEditor);
        break;
    }
}

void RobotEnrichmentDialog::onPropertiesChanged()
{
    QTreeWidgetItem* currentItem = m_robotTreeWidget->currentItem();
    if (!currentItem) return;

    ItemType type = static_cast<ItemType>(currentItem->data(0, ItemTypeRole).toInt());

    // The editor widget holds a direct pointer to the link in our m_robotDescription,
    // so we just need to call its update function to write the UI values back to our data model.
    if (type == ItemType::Link) {
        m_linkEditor->updateLinkDescription();
    }
    else if (type == ItemType::Joint) {
        // TODO: Implement for joints
    }

    // After updating our data, tell the preview viewport to redraw.
    // Use * to dereference the unique_ptr to get the actual RobotDescription object.
    m_previewViewport->loadRobotForPreview(*m_robotDescription);
}

void RobotEnrichmentDialog::onSave()
{
    // Ensure any pending changes in the property editor are written back before saving.
    onPropertiesChanged();
    accept(); // Closes the dialog with a QDialog::Accepted result.
}
