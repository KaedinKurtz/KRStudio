/********************************************************************************
** Form generated from reading UI file 'propertiespanel.ui'
**
** Created by: Qt User Interface Compiler version 6.8.3
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_PROPERTIESPANEL_H
#define UI_PROPERTIESPANEL_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QSpacerItem>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_PropertiesPanel
{
public:
    QGridLayout *gridLayout_2;
    QVBoxLayout *mainLayout;
    QVBoxLayout *gridLayout;
    QSpacerItem *verticalSpacer;
    QToolButton *addGridButton;

    void setupUi(QWidget *PropertiesPanel)
    {
        if (PropertiesPanel->objectName().isEmpty())
            PropertiesPanel->setObjectName("PropertiesPanel");
        PropertiesPanel->resize(528, 409);
        gridLayout_2 = new QGridLayout(PropertiesPanel);
        gridLayout_2->setObjectName("gridLayout_2");
        gridLayout_2->setContentsMargins(0, 0, 0, 0);
        mainLayout = new QVBoxLayout();
        mainLayout->setObjectName("mainLayout");
        gridLayout = new QVBoxLayout();
        gridLayout->setObjectName("gridLayout");

        mainLayout->addLayout(gridLayout);

        verticalSpacer = new QSpacerItem(20, 40, QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Expanding);

        mainLayout->addItem(verticalSpacer);

        addGridButton = new QToolButton(PropertiesPanel);
        addGridButton->setObjectName("addGridButton");
        QSizePolicy sizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(addGridButton->sizePolicy().hasHeightForWidth());
        addGridButton->setSizePolicy(sizePolicy);
        addGridButton->setToolButtonStyle(Qt::ToolButtonStyle::ToolButtonTextBesideIcon);

        mainLayout->addWidget(addGridButton);


        gridLayout_2->addLayout(mainLayout, 0, 0, 1, 1);


        retranslateUi(PropertiesPanel);

        QMetaObject::connectSlotsByName(PropertiesPanel);
    } // setupUi

    void retranslateUi(QWidget *PropertiesPanel)
    {
        PropertiesPanel->setWindowTitle(QCoreApplication::translate("PropertiesPanel", "Form", nullptr));
        addGridButton->setText(QCoreApplication::translate("PropertiesPanel", "Add New Grid", nullptr));
    } // retranslateUi

};

namespace Ui {
    class PropertiesPanel: public Ui_PropertiesPanel {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_PROPERTIESPANEL_H
