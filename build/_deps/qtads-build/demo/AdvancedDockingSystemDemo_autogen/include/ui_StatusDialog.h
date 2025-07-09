/********************************************************************************
** Form generated from reading UI file 'StatusDialog.ui'
**
** Created by: Qt User Interface Compiler version 6.9.0
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_STATUSDIALOG_H
#define UI_STATUSDIALOG_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialog>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QVBoxLayout>

QT_BEGIN_NAMESPACE

class Ui_CStatusDialogClass
{
public:
    QVBoxLayout *verticalLayout_2;
    QLabel *dockWidgetLabel;
    QComboBox *dockWidgetsComboBox;
    QGroupBox *statusGroupBox;
    QVBoxLayout *verticalLayout;
    QCheckBox *isClosedCheckBox;
    QCheckBox *isFloatingCheckBox;
    QCheckBox *tabbedCheckBox;
    QCheckBox *isCurrentTabCheckBox;
    QGroupBox *flagsGroupBox;
    QVBoxLayout *verticalLayout_3;
    QCheckBox *closableCheckBox;
    QCheckBox *movableCheckBox;
    QCheckBox *floatableCheckBox;
    QCheckBox *deleteOnCloseCheckBox;
    QCheckBox *customCloseHandlingCheckBox;

    void setupUi(QDialog *CStatusDialogClass)
    {
        if (CStatusDialogClass->objectName().isEmpty())
            CStatusDialogClass->setObjectName("CStatusDialogClass");
        CStatusDialogClass->resize(357, 331);
        QSizePolicy sizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Preferred);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(CStatusDialogClass->sizePolicy().hasHeightForWidth());
        CStatusDialogClass->setSizePolicy(sizePolicy);
        verticalLayout_2 = new QVBoxLayout(CStatusDialogClass);
        verticalLayout_2->setObjectName("verticalLayout_2");
        verticalLayout_2->setSizeConstraint(QLayout::SetFixedSize);
        dockWidgetLabel = new QLabel(CStatusDialogClass);
        dockWidgetLabel->setObjectName("dockWidgetLabel");
        QSizePolicy sizePolicy1(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Fixed);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(dockWidgetLabel->sizePolicy().hasHeightForWidth());
        dockWidgetLabel->setSizePolicy(sizePolicy1);

        verticalLayout_2->addWidget(dockWidgetLabel);

        dockWidgetsComboBox = new QComboBox(CStatusDialogClass);
        dockWidgetsComboBox->setObjectName("dockWidgetsComboBox");
        dockWidgetsComboBox->setMinimumSize(QSize(300, 0));

        verticalLayout_2->addWidget(dockWidgetsComboBox);

        statusGroupBox = new QGroupBox(CStatusDialogClass);
        statusGroupBox->setObjectName("statusGroupBox");
        QSizePolicy sizePolicy2(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Minimum);
        sizePolicy2.setHorizontalStretch(0);
        sizePolicy2.setVerticalStretch(0);
        sizePolicy2.setHeightForWidth(statusGroupBox->sizePolicy().hasHeightForWidth());
        statusGroupBox->setSizePolicy(sizePolicy2);
        verticalLayout = new QVBoxLayout(statusGroupBox);
        verticalLayout->setObjectName("verticalLayout");
        isClosedCheckBox = new QCheckBox(statusGroupBox);
        isClosedCheckBox->setObjectName("isClosedCheckBox");

        verticalLayout->addWidget(isClosedCheckBox);

        isFloatingCheckBox = new QCheckBox(statusGroupBox);
        isFloatingCheckBox->setObjectName("isFloatingCheckBox");

        verticalLayout->addWidget(isFloatingCheckBox);

        tabbedCheckBox = new QCheckBox(statusGroupBox);
        tabbedCheckBox->setObjectName("tabbedCheckBox");

        verticalLayout->addWidget(tabbedCheckBox);

        isCurrentTabCheckBox = new QCheckBox(statusGroupBox);
        isCurrentTabCheckBox->setObjectName("isCurrentTabCheckBox");

        verticalLayout->addWidget(isCurrentTabCheckBox);


        verticalLayout_2->addWidget(statusGroupBox);

        flagsGroupBox = new QGroupBox(CStatusDialogClass);
        flagsGroupBox->setObjectName("flagsGroupBox");
        sizePolicy2.setHeightForWidth(flagsGroupBox->sizePolicy().hasHeightForWidth());
        flagsGroupBox->setSizePolicy(sizePolicy2);
        verticalLayout_3 = new QVBoxLayout(flagsGroupBox);
        verticalLayout_3->setObjectName("verticalLayout_3");
        closableCheckBox = new QCheckBox(flagsGroupBox);
        closableCheckBox->setObjectName("closableCheckBox");

        verticalLayout_3->addWidget(closableCheckBox);

        movableCheckBox = new QCheckBox(flagsGroupBox);
        movableCheckBox->setObjectName("movableCheckBox");

        verticalLayout_3->addWidget(movableCheckBox);

        floatableCheckBox = new QCheckBox(flagsGroupBox);
        floatableCheckBox->setObjectName("floatableCheckBox");

        verticalLayout_3->addWidget(floatableCheckBox);

        deleteOnCloseCheckBox = new QCheckBox(flagsGroupBox);
        deleteOnCloseCheckBox->setObjectName("deleteOnCloseCheckBox");

        verticalLayout_3->addWidget(deleteOnCloseCheckBox);

        customCloseHandlingCheckBox = new QCheckBox(flagsGroupBox);
        customCloseHandlingCheckBox->setObjectName("customCloseHandlingCheckBox");

        verticalLayout_3->addWidget(customCloseHandlingCheckBox);


        verticalLayout_2->addWidget(flagsGroupBox);


        retranslateUi(CStatusDialogClass);

        QMetaObject::connectSlotsByName(CStatusDialogClass);
    } // setupUi

    void retranslateUi(QDialog *CStatusDialogClass)
    {
        CStatusDialogClass->setWindowTitle(QCoreApplication::translate("CStatusDialogClass", "Dock Widget Status", nullptr));
        dockWidgetLabel->setText(QCoreApplication::translate("CStatusDialogClass", "Dock Widget:", nullptr));
        statusGroupBox->setTitle(QCoreApplication::translate("CStatusDialogClass", "Status", nullptr));
        isClosedCheckBox->setText(QCoreApplication::translate("CStatusDialogClass", "closed", nullptr));
        isFloatingCheckBox->setText(QCoreApplication::translate("CStatusDialogClass", "floating", nullptr));
        tabbedCheckBox->setText(QCoreApplication::translate("CStatusDialogClass", "tabbed", nullptr));
        isCurrentTabCheckBox->setText(QCoreApplication::translate("CStatusDialogClass", "is current tab", nullptr));
        flagsGroupBox->setTitle(QCoreApplication::translate("CStatusDialogClass", "Feature Flags", nullptr));
        closableCheckBox->setText(QCoreApplication::translate("CStatusDialogClass", "DockWidgetClosable", nullptr));
        movableCheckBox->setText(QCoreApplication::translate("CStatusDialogClass", "DockWidgetMovable", nullptr));
        floatableCheckBox->setText(QCoreApplication::translate("CStatusDialogClass", "DockWidgetFloatable", nullptr));
        deleteOnCloseCheckBox->setText(QCoreApplication::translate("CStatusDialogClass", "DockWidgetDeleteOnClose", nullptr));
        customCloseHandlingCheckBox->setText(QCoreApplication::translate("CStatusDialogClass", "CustomCloseHandling", nullptr));
    } // retranslateUi

};

namespace Ui {
    class CStatusDialogClass: public Ui_CStatusDialogClass {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_STATUSDIALOG_H
