#include "LinkPropertiesWidget.hpp"
#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QGroupBox>
#include <QPushButton>
#include <QFileDialog>

LinkPropertiesWidget::LinkPropertiesWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUI();
    setLink(nullptr); // Start in a disabled state
}

void LinkPropertiesWidget::setupUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(15, 15, 15, 15);

    auto* coreGroup = new QGroupBox("Core Properties", this);
    auto* coreLayout = new QFormLayout(coreGroup);
    m_nameLineEdit = new QLineEdit(this);
    coreLayout->addRow("Name:", m_nameLineEdit);
    m_massSpinBox = new QDoubleSpinBox(this);
    m_massSpinBox->setRange(0.0, 10000.0);
    m_massSpinBox->setDecimals(4);
    m_massSpinBox->setSuffix(" kg");
    coreLayout->addRow("Mass:", m_massSpinBox);

    auto* visualsGroup = new QGroupBox("Visuals", this);
    auto* visualsLayout = new QFormLayout(visualsGroup);

    // --- Mesh Path with Browse Button ---
    m_meshPathLineEdit = new QLineEdit(this);
    m_browseMeshButton = new QPushButton("...", this);
    m_browseMeshButton->setFixedSize(30, 25);
    auto* meshLayout = new QHBoxLayout();
    meshLayout->addWidget(m_meshPathLineEdit);
    meshLayout->addWidget(m_browseMeshButton);
    visualsLayout->addRow("Mesh File:", meshLayout);

    m_visibleCheckBox = new QCheckBox("Is Visible", this);
    visualsLayout->addRow("", m_visibleCheckBox);

    mainLayout->addWidget(coreGroup);
    mainLayout->addWidget(visualsGroup);
    mainLayout->addStretch();

    // --- Connect signals ---
    connect(m_browseMeshButton, &QPushButton::clicked, this, &LinkPropertiesWidget::onBrowseMeshClicked);
    connect(m_nameLineEdit, &QLineEdit::editingFinished, this, &LinkPropertiesWidget::propertiesChanged);
    connect(m_meshPathLineEdit, &QLineEdit::editingFinished, this, &LinkPropertiesWidget::propertiesChanged);
    connect(m_massSpinBox, &QDoubleSpinBox::valueChanged, this, &LinkPropertiesWidget::propertiesChanged);
    connect(m_visibleCheckBox, &QCheckBox::stateChanged, this, &LinkPropertiesWidget::propertiesChanged);
}

void LinkPropertiesWidget::setLink(LinkDescription* linkDesc)
{
    m_currentLink = linkDesc;

    if (!m_currentLink) {
        this->setEnabled(false);
        // Clear fields when no link is selected
        m_nameLineEdit->clear();
        m_meshPathLineEdit->clear();
        m_massSpinBox->setValue(0.0);
        m_visibleCheckBox->setChecked(false);
        return;
    }

    this->setEnabled(true);

    bool oldState = signalsBlocked();
    blockSignals(true); // Block signals on the widget itself

    m_nameLineEdit->setText(QString::fromStdString(m_currentLink->name));
    m_meshPathLineEdit->setText(QString::fromStdString(m_currentLink->mesh_filepath));
    m_massSpinBox->setValue(m_currentLink->mass);
    m_visibleCheckBox->setChecked(m_currentLink->is_visible);

    blockSignals(oldState);
}

// This function is now const and modifies the object pointed to by m_currentLink
void LinkPropertiesWidget::updateLinkDescription() const
{
    if (!m_currentLink) return;

    m_currentLink->name = m_nameLineEdit->text().toStdString();
    m_currentLink->mesh_filepath = m_meshPathLineEdit->text().toStdString();
    m_currentLink->mass = m_massSpinBox->value();
    m_currentLink->is_visible = m_visibleCheckBox->isChecked();
}

void LinkPropertiesWidget::onBrowseMeshClicked()
{
    // Open a file dialog to select mesh files (like .obj, .stl, .fbx)
    QString filePath = QFileDialog::getOpenFileName(this, "Select Mesh File", "", "Mesh Files (*.obj *.stl *.fbx *.dae);;All Files (*)");
    if (!filePath.isEmpty()) {
        m_meshPathLineEdit->setText(filePath); // Set the text of the line edit to the chosen path.
        emit propertiesChanged(); // Emit the signal to trigger a live update.
    }
}
