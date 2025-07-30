/********************************************************************************
** Form generated from reading UI file 'ViewportManagerWidget.ui'
**
** Created by: Qt User Interface Compiler version 6.8.3
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_VIEWPORTMANAGERWIDGET_H
#define UI_VIEWPORTMANAGERWIDGET_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QScrollArea>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_ViewportManagerWidget
{
public:
    QVBoxLayout *verticalLayout;
    QWidget *widget;
    QGridLayout *gridLayout;
    QToolButton *removeViewportButton;
    QToolButton *addViewportButton;
    QToolButton *resetButton;
    QLabel *ViewportNumberLabel;
    QScrollArea *scrollArea;
    QWidget *scrollAreaWidgetContents;
    QGridLayout *gridLayout_3;
    QGridLayout *activeViewportWidget;

    void setupUi(QWidget *ViewportManagerWidget)
    {
        if (ViewportManagerWidget->objectName().isEmpty())
            ViewportManagerWidget->setObjectName("ViewportManagerWidget");
        ViewportManagerWidget->resize(400, 112);
        ViewportManagerWidget->setMaximumSize(QSize(16777215, 112));
        verticalLayout = new QVBoxLayout(ViewportManagerWidget);
        verticalLayout->setSpacing(0);
        verticalLayout->setObjectName("verticalLayout");
        verticalLayout->setContentsMargins(0, 0, 0, 0);
        widget = new QWidget(ViewportManagerWidget);
        widget->setObjectName("widget");
        gridLayout = new QGridLayout(widget);
        gridLayout->setObjectName("gridLayout");
        gridLayout->setHorizontalSpacing(2);
        gridLayout->setVerticalSpacing(4);
        gridLayout->setContentsMargins(2, 0, 2, 0);
        removeViewportButton = new QToolButton(widget);
        removeViewportButton->setObjectName("removeViewportButton");
        QSizePolicy sizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Fixed);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(removeViewportButton->sizePolicy().hasHeightForWidth());
        removeViewportButton->setSizePolicy(sizePolicy);
        removeViewportButton->setMinimumSize(QSize(135, 0));

        gridLayout->addWidget(removeViewportButton, 1, 2, 1, 1);

        addViewportButton = new QToolButton(widget);
        addViewportButton->setObjectName("addViewportButton");
        sizePolicy.setHeightForWidth(addViewportButton->sizePolicy().hasHeightForWidth());
        addViewportButton->setSizePolicy(sizePolicy);
        addViewportButton->setMinimumSize(QSize(135, 0));

        gridLayout->addWidget(addViewportButton, 1, 0, 1, 1);

        resetButton = new QToolButton(widget);
        resetButton->setObjectName("resetButton");
        sizePolicy.setHeightForWidth(resetButton->sizePolicy().hasHeightForWidth());
        resetButton->setSizePolicy(sizePolicy);

        gridLayout->addWidget(resetButton, 0, 0, 1, 3);

        ViewportNumberLabel = new QLabel(widget);
        ViewportNumberLabel->setObjectName("ViewportNumberLabel");
        sizePolicy.setHeightForWidth(ViewportNumberLabel->sizePolicy().hasHeightForWidth());
        ViewportNumberLabel->setSizePolicy(sizePolicy);
        ViewportNumberLabel->setMinimumSize(QSize(30, 0));
        ViewportNumberLabel->setFrameShape(QFrame::Shape::StyledPanel);
        ViewportNumberLabel->setAlignment(Qt::AlignmentFlag::AlignCenter);

        gridLayout->addWidget(ViewportNumberLabel, 1, 1, 1, 1);

        scrollArea = new QScrollArea(widget);
        scrollArea->setObjectName("scrollArea");
        scrollArea->setWidgetResizable(true);
        scrollAreaWidgetContents = new QWidget();
        scrollAreaWidgetContents->setObjectName("scrollAreaWidgetContents");
        scrollAreaWidgetContents->setGeometry(QRect(0, 0, 394, 58));
        gridLayout_3 = new QGridLayout(scrollAreaWidgetContents);
        gridLayout_3->setSpacing(0);
        gridLayout_3->setObjectName("gridLayout_3");
        gridLayout_3->setContentsMargins(0, 0, 0, 0);
        activeViewportWidget = new QGridLayout();
        activeViewportWidget->setObjectName("activeViewportWidget");

        gridLayout_3->addLayout(activeViewportWidget, 0, 0, 1, 1);

        scrollArea->setWidget(scrollAreaWidgetContents);

        gridLayout->addWidget(scrollArea, 2, 0, 1, 3);


        verticalLayout->addWidget(widget);


        retranslateUi(ViewportManagerWidget);

        QMetaObject::connectSlotsByName(ViewportManagerWidget);
    } // setupUi

    void retranslateUi(QWidget *ViewportManagerWidget)
    {
        ViewportManagerWidget->setWindowTitle(QCoreApplication::translate("ViewportManagerWidget", "Form", nullptr));
        removeViewportButton->setText(QCoreApplication::translate("ViewportManagerWidget", "Remove Viewport", nullptr));
        addViewportButton->setText(QCoreApplication::translate("ViewportManagerWidget", "Add Viewport", nullptr));
        resetButton->setText(QCoreApplication::translate("ViewportManagerWidget", "Reset Layout Configuration", nullptr));
        ViewportNumberLabel->setText(QCoreApplication::translate("ViewportManagerWidget", "TextLabel", nullptr));
    } // retranslateUi

};

namespace Ui {
    class ViewportManagerWidget: public Ui_ViewportManagerWidget {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_VIEWPORTMANAGERWIDGET_H
