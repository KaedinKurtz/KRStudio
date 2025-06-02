#include "propertiespanel.h"
#include "ui_propertiespanel.h" // This includes the UI definition from propertiespanel.ui

PropertiesPanel::PropertiesPanel(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::PropertiesPanel) // Initialize the UI pointer
{
    ui->setupUi(this); // Setup the UI designed in propertiespanel.ui

    // You can add C++ specific initialization for this panel here,
    // e.g., connecting signals from buttons in your propertiespanel.ui
    // to slots in this PropertiesPanel class.
}

PropertiesPanel::~PropertiesPanel()
{
    delete ui; // Clean up the UI pointer
}
