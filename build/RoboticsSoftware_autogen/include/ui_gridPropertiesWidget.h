/********************************************************************************
** Form generated from reading UI file 'gridPropertiesWidget.ui'
**
** Created by: Qt User Interface Compiler version 6.8.3
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_GRIDPROPERTIESWIDGET_H
#define UI_GRIDPROPERTIESWIDGET_H

#include <QtCore/QVariant>
#include <QtGui/QIcon>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_gridPropertiesWidget
{
public:
    QGridLayout *gridLayout_2;
    QGridLayout *gridLayout;
    QFrame *line_5;
    QDoubleSpinBox *angleInputEulerY;
    QDoubleSpinBox *angleInputEulerX;
    QLabel *translateLabelX;
    QLabel *label_5;
    QDoubleSpinBox *originInputZ;
    QDoubleSpinBox *angleInputEulerZ;
    QFrame *line_2;
    QFrame *line_7;
    QFrame *line_3;
    QLabel *translateLabelY;
    QLabel *label;
    QWidget *quaternionInput;
    QGridLayout *gridLayout_4;
    QGridLayout *gridLayout_3;
    QLabel *label_9;
    QFrame *line_10;
    QDoubleSpinBox *angleInputQuatY;
    QFrame *line_11;
    QLabel *label_10;
    QDoubleSpinBox *angleInputQuatX;
    QLabel *label_11;
    QDoubleSpinBox *angleInputQuatW;
    QFrame *line_12;
    QLabel *label_12;
    QFrame *line_13;
    QDoubleSpinBox *angleInputQuatZ;
    QFrame *line_19;
    QFrame *line_17;
    QLabel *translateLabelX_2;
    QFrame *line_6;
    QFrame *line_9;
    QFrame *line_4;
    QDoubleSpinBox *originInputX;
    QDoubleSpinBox *originInputY;
    QFrame *line;
    QLabel *label_6;
    QLabel *label_7;
    QLabel *label_8;
    QFrame *line_15;
    QFrame *line_8;
    QWidget *widget;
    QHBoxLayout *horizontalLayout;
    QToolButton *deleteButton;
    QComboBox *unitInputBox;
    QWidget *colorVisibility;
    QGridLayout *gridLayout_5;
    QLabel *level3UnitLabel;
    QLabel *level1UnitLabel;
    QWidget *visibilityWidget4;
    QGridLayout *gridLayout_8;
    QCheckBox *visibilityCheck4;
    QLabel *label_15;
    QFrame *level5ColorFrame;
    QToolButton *colorInput1;
    QWidget *visibilityWidget1;
    QGridLayout *gridLayout_10;
    QCheckBox *visibilityCheck1;
    QWidget *visibilityWidget5;
    QGridLayout *gridLayout_9;
    QCheckBox *visibilityCheck5;
    QFrame *level2ColorFrame;
    QToolButton *colorInput3;
    QToolButton *colorInput4;
    QWidget *visibilityWidget3;
    QGridLayout *gridLayout_7;
    QCheckBox *visibilityCheck3;
    QWidget *visibilityWidget2;
    QGridLayout *gridLayout_6;
    QCheckBox *visibilityCheck2;
    QToolButton *colorInput2;
    QLabel *level2UnitLabel;
    QFrame *level3ColorFrame;
    QFrame *line_16;
    QLabel *label_16;
    QToolButton *colorInput5;
    QFrame *level4ColorFrame;
    QLabel *level5UnitLabel;
    QFrame *level1ColorFrame;
    QLabel *label_14;
    QLabel *level4UnitLabel;
    QLineEdit *gridNameInput;
    QLabel *label_13;
    QWidget *widget_2;
    QGridLayout *gridLayout_11;
    QDoubleSpinBox *lineThicknessBox;
    QFrame *line_20;
    QFrame *line_14;
    QComboBox *visualizationCombo;
    QLabel *label_4;
    QLabel *label_3;
    QWidget *widget_3;
    QGridLayout *gridLayout_12;
    QFrame *xAxisColorFrame;
    QLabel *label_17;
    QFrame *zAxisColorFrame;
    QLabel *label_18;
    QToolButton *xAxisColorButton;
    QToolButton *zAxisColorButton;
    QWidget *widget_4;
    QHBoxLayout *horizontalLayout_2;
    QCheckBox *masterVisibilityCheck;
    QToolButton *gridSnapToggleButton;
    QLabel *label_2;
    QFrame *line_18;

    void setupUi(QWidget *gridPropertiesWidget)
    {
        if (gridPropertiesWidget->objectName().isEmpty())
            gridPropertiesWidget->setObjectName("gridPropertiesWidget");
        gridPropertiesWidget->resize(650, 328);
        QSizePolicy sizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Preferred);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(gridPropertiesWidget->sizePolicy().hasHeightForWidth());
        gridPropertiesWidget->setSizePolicy(sizePolicy);
        gridPropertiesWidget->setMinimumSize(QSize(650, 328));
        gridPropertiesWidget->setMaximumSize(QSize(650, 328));
        gridLayout_2 = new QGridLayout(gridPropertiesWidget);
        gridLayout_2->setSpacing(6);
        gridLayout_2->setContentsMargins(11, 11, 11, 11);
        gridLayout_2->setObjectName("gridLayout_2");
        gridLayout_2->setContentsMargins(0, 0, 0, 0);
        gridLayout = new QGridLayout();
        gridLayout->setSpacing(6);
        gridLayout->setObjectName("gridLayout");
        gridLayout->setHorizontalSpacing(2);
        gridLayout->setVerticalSpacing(0);
        gridLayout->setContentsMargins(0, 0, 0, 0);
        line_5 = new QFrame(gridPropertiesWidget);
        line_5->setObjectName("line_5");
        line_5->setFrameShape(QFrame::Shape::VLine);
        line_5->setFrameShadow(QFrame::Shadow::Sunken);

        gridLayout->addWidget(line_5, 2, 4, 5, 1);

        angleInputEulerY = new QDoubleSpinBox(gridPropertiesWidget);
        angleInputEulerY->setObjectName("angleInputEulerY");

        gridLayout->addWidget(angleInputEulerY, 5, 7, 1, 1);

        angleInputEulerX = new QDoubleSpinBox(gridPropertiesWidget);
        angleInputEulerX->setObjectName("angleInputEulerX");
        angleInputEulerX->setMinimumSize(QSize(80, 0));
        angleInputEulerX->setMaximumSize(QSize(100, 16777215));

        gridLayout->addWidget(angleInputEulerX, 4, 7, 1, 1);

        translateLabelX = new QLabel(gridPropertiesWidget);
        translateLabelX->setObjectName("translateLabelX");
        translateLabelX->setMinimumSize(QSize(50, 0));
        translateLabelX->setMaximumSize(QSize(50, 16777215));
        translateLabelX->setAlignment(Qt::AlignmentFlag::AlignRight|Qt::AlignmentFlag::AlignTrailing|Qt::AlignmentFlag::AlignVCenter);
        translateLabelX->setMargin(6);

        gridLayout->addWidget(translateLabelX, 4, 1, 1, 1);

        label_5 = new QLabel(gridPropertiesWidget);
        label_5->setObjectName("label_5");
        label_5->setAlignment(Qt::AlignmentFlag::AlignCenter);

        gridLayout->addWidget(label_5, 2, 5, 1, 5);

        originInputZ = new QDoubleSpinBox(gridPropertiesWidget);
        originInputZ->setObjectName("originInputZ");

        gridLayout->addWidget(originInputZ, 6, 3, 1, 1);

        angleInputEulerZ = new QDoubleSpinBox(gridPropertiesWidget);
        angleInputEulerZ->setObjectName("angleInputEulerZ");

        gridLayout->addWidget(angleInputEulerZ, 6, 7, 1, 1);

        line_2 = new QFrame(gridPropertiesWidget);
        line_2->setObjectName("line_2");
        line_2->setFrameShape(QFrame::Shape::HLine);
        line_2->setFrameShadow(QFrame::Shadow::Sunken);

        gridLayout->addWidget(line_2, 3, 1, 1, 3);

        line_7 = new QFrame(gridPropertiesWidget);
        line_7->setObjectName("line_7");
        line_7->setFrameShape(QFrame::Shape::VLine);
        line_7->setFrameShadow(QFrame::Shadow::Sunken);

        gridLayout->addWidget(line_7, 5, 6, 1, 1);

        line_3 = new QFrame(gridPropertiesWidget);
        line_3->setObjectName("line_3");
        line_3->setFrameShape(QFrame::Shape::VLine);
        line_3->setFrameShadow(QFrame::Shadow::Sunken);

        gridLayout->addWidget(line_3, 5, 2, 1, 1);

        translateLabelY = new QLabel(gridPropertiesWidget);
        translateLabelY->setObjectName("translateLabelY");
        translateLabelY->setMaximumSize(QSize(50, 16777215));
        translateLabelY->setAlignment(Qt::AlignmentFlag::AlignRight|Qt::AlignmentFlag::AlignTrailing|Qt::AlignmentFlag::AlignVCenter);
        translateLabelY->setMargin(6);

        gridLayout->addWidget(translateLabelY, 5, 1, 1, 1);

        label = new QLabel(gridPropertiesWidget);
        label->setObjectName("label");
        QSizePolicy sizePolicy1(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Fixed);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(label->sizePolicy().hasHeightForWidth());
        label->setSizePolicy(sizePolicy1);
        label->setMinimumSize(QSize(0, 10));
        label->setAlignment(Qt::AlignmentFlag::AlignCenter);

        gridLayout->addWidget(label, 2, 1, 1, 3);

        quaternionInput = new QWidget(gridPropertiesWidget);
        quaternionInput->setObjectName("quaternionInput");
        quaternionInput->setMaximumSize(QSize(300, 16777215));
        gridLayout_4 = new QGridLayout(quaternionInput);
        gridLayout_4->setSpacing(0);
        gridLayout_4->setContentsMargins(11, 11, 11, 11);
        gridLayout_4->setObjectName("gridLayout_4");
        gridLayout_4->setContentsMargins(0, 0, 0, 0);
        gridLayout_3 = new QGridLayout();
        gridLayout_3->setSpacing(6);
        gridLayout_3->setObjectName("gridLayout_3");
        gridLayout_3->setVerticalSpacing(10);
        label_9 = new QLabel(quaternionInput);
        label_9->setObjectName("label_9");
        label_9->setMinimumSize(QSize(40, 0));
        label_9->setMaximumSize(QSize(40, 16777215));
        label_9->setAlignment(Qt::AlignmentFlag::AlignRight|Qt::AlignmentFlag::AlignTrailing|Qt::AlignmentFlag::AlignVCenter);
        label_9->setMargin(6);

        gridLayout_3->addWidget(label_9, 0, 0, 1, 1);

        line_10 = new QFrame(quaternionInput);
        line_10->setObjectName("line_10");
        line_10->setFrameShape(QFrame::Shape::VLine);
        line_10->setFrameShadow(QFrame::Shadow::Sunken);

        gridLayout_3->addWidget(line_10, 0, 1, 1, 1);

        angleInputQuatY = new QDoubleSpinBox(quaternionInput);
        angleInputQuatY->setObjectName("angleInputQuatY");
        QSizePolicy sizePolicy2(QSizePolicy::Policy::Minimum, QSizePolicy::Policy::Fixed);
        sizePolicy2.setHorizontalStretch(0);
        sizePolicy2.setVerticalStretch(0);
        sizePolicy2.setHeightForWidth(angleInputQuatY->sizePolicy().hasHeightForWidth());
        angleInputQuatY->setSizePolicy(sizePolicy2);
        angleInputQuatY->setMinimumSize(QSize(80, 0));

        gridLayout_3->addWidget(angleInputQuatY, 1, 2, 1, 1);

        line_11 = new QFrame(quaternionInput);
        line_11->setObjectName("line_11");
        line_11->setFrameShape(QFrame::Shape::VLine);
        line_11->setFrameShadow(QFrame::Shadow::Sunken);

        gridLayout_3->addWidget(line_11, 1, 1, 1, 1);

        label_10 = new QLabel(quaternionInput);
        label_10->setObjectName("label_10");
        label_10->setMinimumSize(QSize(40, 0));
        label_10->setAlignment(Qt::AlignmentFlag::AlignRight|Qt::AlignmentFlag::AlignTrailing|Qt::AlignmentFlag::AlignVCenter);
        label_10->setMargin(6);

        gridLayout_3->addWidget(label_10, 1, 0, 1, 1);

        angleInputQuatX = new QDoubleSpinBox(quaternionInput);
        angleInputQuatX->setObjectName("angleInputQuatX");
        angleInputQuatX->setMinimumSize(QSize(80, 0));

        gridLayout_3->addWidget(angleInputQuatX, 0, 5, 1, 1);

        label_11 = new QLabel(quaternionInput);
        label_11->setObjectName("label_11");
        label_11->setMinimumSize(QSize(40, 0));
        label_11->setMaximumSize(QSize(40, 16777215));
        label_11->setAlignment(Qt::AlignmentFlag::AlignRight|Qt::AlignmentFlag::AlignTrailing|Qt::AlignmentFlag::AlignVCenter);
        label_11->setMargin(6);

        gridLayout_3->addWidget(label_11, 0, 3, 1, 1);

        angleInputQuatW = new QDoubleSpinBox(quaternionInput);
        angleInputQuatW->setObjectName("angleInputQuatW");
        angleInputQuatW->setMinimumSize(QSize(80, 0));

        gridLayout_3->addWidget(angleInputQuatW, 0, 2, 1, 1);

        line_12 = new QFrame(quaternionInput);
        line_12->setObjectName("line_12");
        line_12->setFrameShape(QFrame::Shape::VLine);
        line_12->setFrameShadow(QFrame::Shadow::Sunken);

        gridLayout_3->addWidget(line_12, 0, 4, 1, 1);

        label_12 = new QLabel(quaternionInput);
        label_12->setObjectName("label_12");
        label_12->setMinimumSize(QSize(40, 0));
        label_12->setAlignment(Qt::AlignmentFlag::AlignRight|Qt::AlignmentFlag::AlignTrailing|Qt::AlignmentFlag::AlignVCenter);
        label_12->setMargin(6);

        gridLayout_3->addWidget(label_12, 1, 3, 1, 1);

        line_13 = new QFrame(quaternionInput);
        line_13->setObjectName("line_13");
        line_13->setFrameShape(QFrame::Shape::VLine);
        line_13->setFrameShadow(QFrame::Shadow::Sunken);

        gridLayout_3->addWidget(line_13, 1, 4, 1, 1);

        angleInputQuatZ = new QDoubleSpinBox(quaternionInput);
        angleInputQuatZ->setObjectName("angleInputQuatZ");
        angleInputQuatZ->setMinimumSize(QSize(80, 0));

        gridLayout_3->addWidget(angleInputQuatZ, 1, 5, 1, 1);


        gridLayout_4->addLayout(gridLayout_3, 0, 0, 1, 1);


        gridLayout->addWidget(quaternionInput, 4, 9, 3, 1);

        line_19 = new QFrame(gridPropertiesWidget);
        line_19->setObjectName("line_19");
        line_19->setFrameShape(QFrame::Shape::HLine);
        line_19->setFrameShadow(QFrame::Shadow::Sunken);

        gridLayout->addWidget(line_19, 7, 1, 1, 3);

        line_17 = new QFrame(gridPropertiesWidget);
        line_17->setObjectName("line_17");
        line_17->setFrameShape(QFrame::Shape::VLine);
        line_17->setFrameShadow(QFrame::Shadow::Sunken);

        gridLayout->addWidget(line_17, 4, 8, 3, 1);

        translateLabelX_2 = new QLabel(gridPropertiesWidget);
        translateLabelX_2->setObjectName("translateLabelX_2");
        translateLabelX_2->setMaximumSize(QSize(50, 16777215));
        translateLabelX_2->setAlignment(Qt::AlignmentFlag::AlignRight|Qt::AlignmentFlag::AlignTrailing|Qt::AlignmentFlag::AlignVCenter);
        translateLabelX_2->setMargin(6);

        gridLayout->addWidget(translateLabelX_2, 6, 1, 1, 1);

        line_6 = new QFrame(gridPropertiesWidget);
        line_6->setObjectName("line_6");
        line_6->setFrameShape(QFrame::Shape::VLine);
        line_6->setFrameShadow(QFrame::Shadow::Sunken);

        gridLayout->addWidget(line_6, 6, 6, 1, 1);

        line_9 = new QFrame(gridPropertiesWidget);
        line_9->setObjectName("line_9");
        line_9->setFrameShape(QFrame::Shape::HLine);
        line_9->setFrameShadow(QFrame::Shadow::Sunken);

        gridLayout->addWidget(line_9, 3, 5, 1, 5);

        line_4 = new QFrame(gridPropertiesWidget);
        line_4->setObjectName("line_4");
        line_4->setFrameShape(QFrame::Shape::VLine);
        line_4->setFrameShadow(QFrame::Shadow::Sunken);

        gridLayout->addWidget(line_4, 6, 2, 1, 1);

        originInputX = new QDoubleSpinBox(gridPropertiesWidget);
        originInputX->setObjectName("originInputX");
        originInputX->setMinimumSize(QSize(80, 0));
        originInputX->setMaximumSize(QSize(100, 16777215));

        gridLayout->addWidget(originInputX, 4, 3, 1, 1);

        originInputY = new QDoubleSpinBox(gridPropertiesWidget);
        originInputY->setObjectName("originInputY");

        gridLayout->addWidget(originInputY, 5, 3, 1, 1);

        line = new QFrame(gridPropertiesWidget);
        line->setObjectName("line");
        line->setFrameShape(QFrame::Shape::VLine);
        line->setFrameShadow(QFrame::Shadow::Sunken);

        gridLayout->addWidget(line, 4, 2, 1, 1);

        label_6 = new QLabel(gridPropertiesWidget);
        label_6->setObjectName("label_6");
        label_6->setMinimumSize(QSize(65, 0));
        label_6->setMaximumSize(QSize(65, 16777215));
        label_6->setAlignment(Qt::AlignmentFlag::AlignRight|Qt::AlignmentFlag::AlignTrailing|Qt::AlignmentFlag::AlignVCenter);
        label_6->setMargin(6);

        gridLayout->addWidget(label_6, 4, 5, 1, 1);

        label_7 = new QLabel(gridPropertiesWidget);
        label_7->setObjectName("label_7");
        label_7->setMinimumSize(QSize(50, 0));
        label_7->setMaximumSize(QSize(65, 16777215));
        label_7->setAlignment(Qt::AlignmentFlag::AlignRight|Qt::AlignmentFlag::AlignTrailing|Qt::AlignmentFlag::AlignVCenter);
        label_7->setMargin(6);

        gridLayout->addWidget(label_7, 5, 5, 1, 1);

        label_8 = new QLabel(gridPropertiesWidget);
        label_8->setObjectName("label_8");
        label_8->setMinimumSize(QSize(65, 0));
        label_8->setMaximumSize(QSize(65, 16777215));
        label_8->setAlignment(Qt::AlignmentFlag::AlignRight|Qt::AlignmentFlag::AlignTrailing|Qt::AlignmentFlag::AlignVCenter);
        label_8->setMargin(6);

        gridLayout->addWidget(label_8, 6, 5, 1, 1);

        line_15 = new QFrame(gridPropertiesWidget);
        line_15->setObjectName("line_15");
        line_15->setFrameShape(QFrame::Shape::HLine);
        line_15->setFrameShadow(QFrame::Shadow::Sunken);

        gridLayout->addWidget(line_15, 1, 1, 1, 8);

        line_8 = new QFrame(gridPropertiesWidget);
        line_8->setObjectName("line_8");
        line_8->setFrameShape(QFrame::Shape::VLine);
        line_8->setFrameShadow(QFrame::Shadow::Sunken);

        gridLayout->addWidget(line_8, 4, 6, 1, 1);

        widget = new QWidget(gridPropertiesWidget);
        widget->setObjectName("widget");
        horizontalLayout = new QHBoxLayout(widget);
        horizontalLayout->setSpacing(6);
        horizontalLayout->setContentsMargins(11, 11, 11, 11);
        horizontalLayout->setObjectName("horizontalLayout");
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        deleteButton = new QToolButton(widget);
        deleteButton->setObjectName("deleteButton");
        QIcon icon;
        icon.addFile(QString::fromUtf8(":/icons/icons8-close-48.png"), QSize(), QIcon::Mode::Normal, QIcon::State::Off);
        deleteButton->setIcon(icon);
        deleteButton->setToolButtonStyle(Qt::ToolButtonStyle::ToolButtonTextBesideIcon);

        horizontalLayout->addWidget(deleteButton);

        unitInputBox = new QComboBox(widget);
        unitInputBox->addItem(QString());
        unitInputBox->addItem(QString());
        unitInputBox->setObjectName("unitInputBox");
        unitInputBox->setMinimumSize(QSize(200, 0));

        horizontalLayout->addWidget(unitInputBox);


        gridLayout->addWidget(widget, 0, 9, 1, 1);

        colorVisibility = new QWidget(gridPropertiesWidget);
        colorVisibility->setObjectName("colorVisibility");
        colorVisibility->setMinimumSize(QSize(0, 150));
        colorVisibility->setMaximumSize(QSize(15555, 150));
        gridLayout_5 = new QGridLayout(colorVisibility);
        gridLayout_5->setSpacing(6);
        gridLayout_5->setContentsMargins(11, 11, 11, 11);
        gridLayout_5->setObjectName("gridLayout_5");
        gridLayout_5->setContentsMargins(6, 2, 6, 4);
        level3UnitLabel = new QLabel(colorVisibility);
        level3UnitLabel->setObjectName("level3UnitLabel");
        level3UnitLabel->setAlignment(Qt::AlignmentFlag::AlignRight|Qt::AlignmentFlag::AlignTrailing|Qt::AlignmentFlag::AlignVCenter);

        gridLayout_5->addWidget(level3UnitLabel, 5, 0, 1, 1);

        level1UnitLabel = new QLabel(colorVisibility);
        level1UnitLabel->setObjectName("level1UnitLabel");
        QSizePolicy sizePolicy3(QSizePolicy::Policy::Fixed, QSizePolicy::Policy::Preferred);
        sizePolicy3.setHorizontalStretch(0);
        sizePolicy3.setVerticalStretch(0);
        sizePolicy3.setHeightForWidth(level1UnitLabel->sizePolicy().hasHeightForWidth());
        level1UnitLabel->setSizePolicy(sizePolicy3);
        level1UnitLabel->setMinimumSize(QSize(60, 0));
        level1UnitLabel->setAlignment(Qt::AlignmentFlag::AlignRight|Qt::AlignmentFlag::AlignTrailing|Qt::AlignmentFlag::AlignVCenter);

        gridLayout_5->addWidget(level1UnitLabel, 3, 0, 1, 1);

        visibilityWidget4 = new QWidget(colorVisibility);
        visibilityWidget4->setObjectName("visibilityWidget4");
        gridLayout_8 = new QGridLayout(visibilityWidget4);
        gridLayout_8->setSpacing(0);
        gridLayout_8->setContentsMargins(11, 11, 11, 11);
        gridLayout_8->setObjectName("gridLayout_8");
        gridLayout_8->setContentsMargins(0, 0, 0, 0);
        visibilityCheck4 = new QCheckBox(visibilityWidget4);
        visibilityCheck4->setObjectName("visibilityCheck4");
        QSizePolicy sizePolicy4(QSizePolicy::Policy::Fixed, QSizePolicy::Policy::Fixed);
        sizePolicy4.setHorizontalStretch(0);
        sizePolicy4.setVerticalStretch(0);
        sizePolicy4.setHeightForWidth(visibilityCheck4->sizePolicy().hasHeightForWidth());
        visibilityCheck4->setSizePolicy(sizePolicy4);
        visibilityCheck4->setMinimumSize(QSize(15, 15));

        gridLayout_8->addWidget(visibilityCheck4, 0, 0, 1, 1);


        gridLayout_5->addWidget(visibilityWidget4, 6, 1, 1, 1);

        label_15 = new QLabel(colorVisibility);
        label_15->setObjectName("label_15");
        label_15->setMinimumSize(QSize(80, 0));
        label_15->setMaximumSize(QSize(80, 16777215));
        label_15->setAlignment(Qt::AlignmentFlag::AlignCenter);

        gridLayout_5->addWidget(label_15, 1, 1, 1, 1);

        level5ColorFrame = new QFrame(colorVisibility);
        level5ColorFrame->setObjectName("level5ColorFrame");
        level5ColorFrame->setMinimumSize(QSize(20, 20));
        level5ColorFrame->setFrameShape(QFrame::Shape::StyledPanel);
        level5ColorFrame->setFrameShadow(QFrame::Shadow::Raised);

        gridLayout_5->addWidget(level5ColorFrame, 7, 2, 1, 1);

        colorInput1 = new QToolButton(colorVisibility);
        colorInput1->setObjectName("colorInput1");
        colorInput1->setMinimumSize(QSize(120, 0));

        gridLayout_5->addWidget(colorInput1, 3, 3, 1, 1);

        visibilityWidget1 = new QWidget(colorVisibility);
        visibilityWidget1->setObjectName("visibilityWidget1");
        gridLayout_10 = new QGridLayout(visibilityWidget1);
        gridLayout_10->setSpacing(6);
        gridLayout_10->setContentsMargins(11, 11, 11, 11);
        gridLayout_10->setObjectName("gridLayout_10");
        gridLayout_10->setContentsMargins(0, 0, 0, 0);
        visibilityCheck1 = new QCheckBox(visibilityWidget1);
        visibilityCheck1->setObjectName("visibilityCheck1");
        sizePolicy4.setHeightForWidth(visibilityCheck1->sizePolicy().hasHeightForWidth());
        visibilityCheck1->setSizePolicy(sizePolicy4);
        visibilityCheck1->setMinimumSize(QSize(15, 15));
        visibilityCheck1->setTristate(false);

        gridLayout_10->addWidget(visibilityCheck1, 0, 0, 1, 1);


        gridLayout_5->addWidget(visibilityWidget1, 3, 1, 1, 1);

        visibilityWidget5 = new QWidget(colorVisibility);
        visibilityWidget5->setObjectName("visibilityWidget5");
        gridLayout_9 = new QGridLayout(visibilityWidget5);
        gridLayout_9->setSpacing(0);
        gridLayout_9->setContentsMargins(11, 11, 11, 11);
        gridLayout_9->setObjectName("gridLayout_9");
        gridLayout_9->setContentsMargins(0, 0, 0, 0);
        visibilityCheck5 = new QCheckBox(visibilityWidget5);
        visibilityCheck5->setObjectName("visibilityCheck5");
        sizePolicy4.setHeightForWidth(visibilityCheck5->sizePolicy().hasHeightForWidth());
        visibilityCheck5->setSizePolicy(sizePolicy4);
        visibilityCheck5->setMinimumSize(QSize(15, 15));

        gridLayout_9->addWidget(visibilityCheck5, 0, 0, 1, 1);


        gridLayout_5->addWidget(visibilityWidget5, 7, 1, 1, 1);

        level2ColorFrame = new QFrame(colorVisibility);
        level2ColorFrame->setObjectName("level2ColorFrame");
        level2ColorFrame->setMinimumSize(QSize(20, 20));
        level2ColorFrame->setFrameShape(QFrame::Shape::StyledPanel);
        level2ColorFrame->setFrameShadow(QFrame::Shadow::Raised);

        gridLayout_5->addWidget(level2ColorFrame, 4, 2, 1, 1);

        colorInput3 = new QToolButton(colorVisibility);
        colorInput3->setObjectName("colorInput3");
        colorInput3->setMinimumSize(QSize(120, 0));

        gridLayout_5->addWidget(colorInput3, 5, 3, 1, 1);

        colorInput4 = new QToolButton(colorVisibility);
        colorInput4->setObjectName("colorInput4");
        colorInput4->setMinimumSize(QSize(120, 0));

        gridLayout_5->addWidget(colorInput4, 6, 3, 1, 1);

        visibilityWidget3 = new QWidget(colorVisibility);
        visibilityWidget3->setObjectName("visibilityWidget3");
        gridLayout_7 = new QGridLayout(visibilityWidget3);
        gridLayout_7->setSpacing(0);
        gridLayout_7->setContentsMargins(11, 11, 11, 11);
        gridLayout_7->setObjectName("gridLayout_7");
        gridLayout_7->setContentsMargins(0, 0, 0, 0);
        visibilityCheck3 = new QCheckBox(visibilityWidget3);
        visibilityCheck3->setObjectName("visibilityCheck3");
        sizePolicy4.setHeightForWidth(visibilityCheck3->sizePolicy().hasHeightForWidth());
        visibilityCheck3->setSizePolicy(sizePolicy4);
        visibilityCheck3->setMinimumSize(QSize(15, 15));

        gridLayout_7->addWidget(visibilityCheck3, 0, 0, 1, 1);


        gridLayout_5->addWidget(visibilityWidget3, 5, 1, 1, 1);

        visibilityWidget2 = new QWidget(colorVisibility);
        visibilityWidget2->setObjectName("visibilityWidget2");
        gridLayout_6 = new QGridLayout(visibilityWidget2);
        gridLayout_6->setSpacing(0);
        gridLayout_6->setContentsMargins(11, 11, 11, 11);
        gridLayout_6->setObjectName("gridLayout_6");
        gridLayout_6->setContentsMargins(0, 0, 0, 0);
        visibilityCheck2 = new QCheckBox(visibilityWidget2);
        visibilityCheck2->setObjectName("visibilityCheck2");
        sizePolicy4.setHeightForWidth(visibilityCheck2->sizePolicy().hasHeightForWidth());
        visibilityCheck2->setSizePolicy(sizePolicy4);
        visibilityCheck2->setMinimumSize(QSize(15, 15));

        gridLayout_6->addWidget(visibilityCheck2, 0, 0, 1, 1);


        gridLayout_5->addWidget(visibilityWidget2, 4, 1, 1, 1);

        colorInput2 = new QToolButton(colorVisibility);
        colorInput2->setObjectName("colorInput2");
        colorInput2->setMinimumSize(QSize(120, 0));

        gridLayout_5->addWidget(colorInput2, 4, 3, 1, 1);

        level2UnitLabel = new QLabel(colorVisibility);
        level2UnitLabel->setObjectName("level2UnitLabel");
        level2UnitLabel->setAlignment(Qt::AlignmentFlag::AlignRight|Qt::AlignmentFlag::AlignTrailing|Qt::AlignmentFlag::AlignVCenter);

        gridLayout_5->addWidget(level2UnitLabel, 4, 0, 1, 1);

        level3ColorFrame = new QFrame(colorVisibility);
        level3ColorFrame->setObjectName("level3ColorFrame");
        level3ColorFrame->setMinimumSize(QSize(20, 20));
        level3ColorFrame->setFrameShape(QFrame::Shape::StyledPanel);
        level3ColorFrame->setFrameShadow(QFrame::Shadow::Raised);

        gridLayout_5->addWidget(level3ColorFrame, 5, 2, 1, 1);

        line_16 = new QFrame(colorVisibility);
        line_16->setObjectName("line_16");
        line_16->setFrameShape(QFrame::Shape::HLine);
        line_16->setFrameShadow(QFrame::Shadow::Sunken);

        gridLayout_5->addWidget(line_16, 2, 0, 1, 4);

        label_16 = new QLabel(colorVisibility);
        label_16->setObjectName("label_16");
        label_16->setAlignment(Qt::AlignmentFlag::AlignCenter);

        gridLayout_5->addWidget(label_16, 1, 2, 1, 2);

        colorInput5 = new QToolButton(colorVisibility);
        colorInput5->setObjectName("colorInput5");
        colorInput5->setMinimumSize(QSize(120, 0));

        gridLayout_5->addWidget(colorInput5, 7, 3, 1, 1);

        level4ColorFrame = new QFrame(colorVisibility);
        level4ColorFrame->setObjectName("level4ColorFrame");
        level4ColorFrame->setMinimumSize(QSize(20, 20));
        level4ColorFrame->setFrameShape(QFrame::Shape::StyledPanel);
        level4ColorFrame->setFrameShadow(QFrame::Shadow::Raised);

        gridLayout_5->addWidget(level4ColorFrame, 6, 2, 1, 1);

        level5UnitLabel = new QLabel(colorVisibility);
        level5UnitLabel->setObjectName("level5UnitLabel");
        level5UnitLabel->setAlignment(Qt::AlignmentFlag::AlignRight|Qt::AlignmentFlag::AlignTrailing|Qt::AlignmentFlag::AlignVCenter);

        gridLayout_5->addWidget(level5UnitLabel, 7, 0, 1, 1);

        level1ColorFrame = new QFrame(colorVisibility);
        level1ColorFrame->setObjectName("level1ColorFrame");
        QSizePolicy sizePolicy5(QSizePolicy::Policy::Maximum, QSizePolicy::Policy::Preferred);
        sizePolicy5.setHorizontalStretch(0);
        sizePolicy5.setVerticalStretch(0);
        sizePolicy5.setHeightForWidth(level1ColorFrame->sizePolicy().hasHeightForWidth());
        level1ColorFrame->setSizePolicy(sizePolicy5);
        level1ColorFrame->setMinimumSize(QSize(20, 20));
        level1ColorFrame->setFrameShape(QFrame::Shape::StyledPanel);
        level1ColorFrame->setFrameShadow(QFrame::Shadow::Raised);

        gridLayout_5->addWidget(level1ColorFrame, 3, 2, 1, 1);

        label_14 = new QLabel(colorVisibility);
        label_14->setObjectName("label_14");
        label_14->setMinimumSize(QSize(60, 0));
        label_14->setAlignment(Qt::AlignmentFlag::AlignCenter);

        gridLayout_5->addWidget(label_14, 1, 0, 1, 1);

        level4UnitLabel = new QLabel(colorVisibility);
        level4UnitLabel->setObjectName("level4UnitLabel");
        level4UnitLabel->setAlignment(Qt::AlignmentFlag::AlignRight|Qt::AlignmentFlag::AlignTrailing|Qt::AlignmentFlag::AlignVCenter);

        gridLayout_5->addWidget(level4UnitLabel, 6, 0, 1, 1);


        gridLayout->addWidget(colorVisibility, 10, 1, 1, 8);

        gridNameInput = new QLineEdit(gridPropertiesWidget);
        gridNameInput->setObjectName("gridNameInput");

        gridLayout->addWidget(gridNameInput, 0, 1, 1, 8);

        label_13 = new QLabel(gridPropertiesWidget);
        label_13->setObjectName("label_13");
        label_13->setMinimumSize(QSize(0, 25));
        label_13->setMaximumSize(QSize(16777215, 25));
        label_13->setAlignment(Qt::AlignmentFlag::AlignCenter);

        gridLayout->addWidget(label_13, 8, 1, 1, 8);

        widget_2 = new QWidget(gridPropertiesWidget);
        widget_2->setObjectName("widget_2");
        gridLayout_11 = new QGridLayout(widget_2);
        gridLayout_11->setSpacing(6);
        gridLayout_11->setContentsMargins(11, 11, 11, 11);
        gridLayout_11->setObjectName("gridLayout_11");
        lineThicknessBox = new QDoubleSpinBox(widget_2);
        lineThicknessBox->setObjectName("lineThicknessBox");

        gridLayout_11->addWidget(lineThicknessBox, 0, 2, 1, 1);

        line_20 = new QFrame(widget_2);
        line_20->setObjectName("line_20");
        line_20->setFrameShape(QFrame::Shape::VLine);
        line_20->setFrameShadow(QFrame::Shadow::Sunken);

        gridLayout_11->addWidget(line_20, 1, 1, 1, 1);

        line_14 = new QFrame(widget_2);
        line_14->setObjectName("line_14");
        line_14->setFrameShape(QFrame::Shape::VLine);
        line_14->setFrameShadow(QFrame::Shadow::Sunken);

        gridLayout_11->addWidget(line_14, 0, 1, 1, 1);

        visualizationCombo = new QComboBox(widget_2);
        visualizationCombo->addItem(QString());
        visualizationCombo->addItem(QString());
        visualizationCombo->setObjectName("visualizationCombo");

        gridLayout_11->addWidget(visualizationCombo, 1, 2, 1, 1);

        label_4 = new QLabel(widget_2);
        label_4->setObjectName("label_4");

        gridLayout_11->addWidget(label_4, 1, 0, 1, 1);

        label_3 = new QLabel(widget_2);
        label_3->setObjectName("label_3");
        label_3->setMaximumSize(QSize(80, 16777215));

        gridLayout_11->addWidget(label_3, 0, 0, 1, 1);

        widget_3 = new QWidget(widget_2);
        widget_3->setObjectName("widget_3");
        gridLayout_12 = new QGridLayout(widget_3);
        gridLayout_12->setSpacing(6);
        gridLayout_12->setContentsMargins(11, 11, 11, 11);
        gridLayout_12->setObjectName("gridLayout_12");
        xAxisColorFrame = new QFrame(widget_3);
        xAxisColorFrame->setObjectName("xAxisColorFrame");
        xAxisColorFrame->setMinimumSize(QSize(20, 20));
        xAxisColorFrame->setMaximumSize(QSize(20, 20));
        xAxisColorFrame->setFrameShape(QFrame::Shape::StyledPanel);
        xAxisColorFrame->setFrameShadow(QFrame::Shadow::Raised);

        gridLayout_12->addWidget(xAxisColorFrame, 0, 1, 1, 1);

        label_17 = new QLabel(widget_3);
        label_17->setObjectName("label_17");

        gridLayout_12->addWidget(label_17, 0, 0, 1, 1);

        zAxisColorFrame = new QFrame(widget_3);
        zAxisColorFrame->setObjectName("zAxisColorFrame");
        zAxisColorFrame->setMinimumSize(QSize(20, 20));
        zAxisColorFrame->setMaximumSize(QSize(20, 20));
        zAxisColorFrame->setFrameShape(QFrame::Shape::StyledPanel);
        zAxisColorFrame->setFrameShadow(QFrame::Shadow::Raised);

        gridLayout_12->addWidget(zAxisColorFrame, 1, 1, 1, 1);

        label_18 = new QLabel(widget_3);
        label_18->setObjectName("label_18");

        gridLayout_12->addWidget(label_18, 1, 0, 1, 1);

        xAxisColorButton = new QToolButton(widget_3);
        xAxisColorButton->setObjectName("xAxisColorButton");
        xAxisColorButton->setMinimumSize(QSize(0, 20));

        gridLayout_12->addWidget(xAxisColorButton, 0, 2, 1, 1);

        zAxisColorButton = new QToolButton(widget_3);
        zAxisColorButton->setObjectName("zAxisColorButton");
        zAxisColorButton->setMinimumSize(QSize(0, 20));

        gridLayout_12->addWidget(zAxisColorButton, 1, 2, 1, 1);


        gridLayout_11->addWidget(widget_3, 2, 0, 1, 3);

        widget_4 = new QWidget(widget_2);
        widget_4->setObjectName("widget_4");
        widget_4->setMinimumSize(QSize(280, 20));
        horizontalLayout_2 = new QHBoxLayout(widget_4);
        horizontalLayout_2->setSpacing(6);
        horizontalLayout_2->setContentsMargins(11, 11, 11, 11);
        horizontalLayout_2->setObjectName("horizontalLayout_2");
        horizontalLayout_2->setContentsMargins(0, 0, 0, 0);
        masterVisibilityCheck = new QCheckBox(widget_4);
        masterVisibilityCheck->setObjectName("masterVisibilityCheck");
        masterVisibilityCheck->setMaximumSize(QSize(130, 16777215));

        horizontalLayout_2->addWidget(masterVisibilityCheck);

        gridSnapToggleButton = new QToolButton(widget_4);
        gridSnapToggleButton->setObjectName("gridSnapToggleButton");
        gridSnapToggleButton->setMinimumSize(QSize(140, 0));
        gridSnapToggleButton->setCheckable(true);

        horizontalLayout_2->addWidget(gridSnapToggleButton);


        gridLayout_11->addWidget(widget_4, 3, 0, 1, 3);


        gridLayout->addWidget(widget_2, 10, 9, 1, 1);

        label_2 = new QLabel(gridPropertiesWidget);
        label_2->setObjectName("label_2");
        label_2->setAlignment(Qt::AlignmentFlag::AlignCenter);

        gridLayout->addWidget(label_2, 8, 9, 1, 1);

        line_18 = new QFrame(gridPropertiesWidget);
        line_18->setObjectName("line_18");
        line_18->setFrameShape(QFrame::Shape::HLine);
        line_18->setFrameShadow(QFrame::Shadow::Sunken);

        gridLayout->addWidget(line_18, 9, 1, 1, 9);


        gridLayout_2->addLayout(gridLayout, 0, 0, 1, 1);


        retranslateUi(gridPropertiesWidget);

        QMetaObject::connectSlotsByName(gridPropertiesWidget);
    } // setupUi

    void retranslateUi(QWidget *gridPropertiesWidget)
    {
        gridPropertiesWidget->setWindowTitle(QCoreApplication::translate("gridPropertiesWidget", "gridPropertiesWidget", nullptr));
        translateLabelX->setText(QCoreApplication::translate("gridPropertiesWidget", "X (m)", nullptr));
        label_5->setText(QCoreApplication::translate("gridPropertiesWidget", "Orientation", nullptr));
        translateLabelY->setText(QCoreApplication::translate("gridPropertiesWidget", "Y (m)", nullptr));
        label->setText(QCoreApplication::translate("gridPropertiesWidget", "Origin", nullptr));
        label_9->setText(QCoreApplication::translate("gridPropertiesWidget", "W", nullptr));
        label_10->setText(QCoreApplication::translate("gridPropertiesWidget", "Y", nullptr));
        label_11->setText(QCoreApplication::translate("gridPropertiesWidget", "X", nullptr));
        label_12->setText(QCoreApplication::translate("gridPropertiesWidget", "Z", nullptr));
        translateLabelX_2->setText(QCoreApplication::translate("gridPropertiesWidget", "Z (m)", nullptr));
        label_6->setText(QCoreApplication::translate("gridPropertiesWidget", "Roll (X\302\260)", nullptr));
        label_7->setText(QCoreApplication::translate("gridPropertiesWidget", "Pitch (Y\302\260)", nullptr));
        label_8->setText(QCoreApplication::translate("gridPropertiesWidget", "Yaw (Z\302\260)", nullptr));
        deleteButton->setText(QCoreApplication::translate("gridPropertiesWidget", "Delete Grid", nullptr));
        unitInputBox->setItemText(0, QCoreApplication::translate("gridPropertiesWidget", "Metric (mm, cm, m)", nullptr));
        unitInputBox->setItemText(1, QCoreApplication::translate("gridPropertiesWidget", "Imperial (In, yd, mil)", nullptr));

        level3UnitLabel->setText(QCoreApplication::translate("gridPropertiesWidget", "10cm", nullptr));
        level1UnitLabel->setText(QCoreApplication::translate("gridPropertiesWidget", "1mm", nullptr));
        visibilityCheck4->setText(QString());
        label_15->setText(QCoreApplication::translate("gridPropertiesWidget", "Visibility", nullptr));
        colorInput1->setText(QCoreApplication::translate("gridPropertiesWidget", "Change Grid Color", nullptr));
        visibilityCheck5->setText(QString());
        colorInput3->setText(QCoreApplication::translate("gridPropertiesWidget", "Change Grid Color", nullptr));
        colorInput4->setText(QCoreApplication::translate("gridPropertiesWidget", "Change Grid Color", nullptr));
        visibilityCheck3->setText(QString());
        visibilityCheck2->setText(QString());
        colorInput2->setText(QCoreApplication::translate("gridPropertiesWidget", "Change Grid Color", nullptr));
        level2UnitLabel->setText(QCoreApplication::translate("gridPropertiesWidget", "1cm", nullptr));
        label_16->setText(QCoreApplication::translate("gridPropertiesWidget", "Color", nullptr));
        colorInput5->setText(QCoreApplication::translate("gridPropertiesWidget", "Change Grid Color", nullptr));
        level5UnitLabel->setText(QCoreApplication::translate("gridPropertiesWidget", "10m", nullptr));
        label_14->setText(QCoreApplication::translate("gridPropertiesWidget", "Box Size", nullptr));
        level4UnitLabel->setText(QCoreApplication::translate("gridPropertiesWidget", "1m", nullptr));
        label_13->setText(QCoreApplication::translate("gridPropertiesWidget", "Color / Visibility", nullptr));
        visualizationCombo->setItemText(0, QCoreApplication::translate("gridPropertiesWidget", "Lines", nullptr));
        visualizationCombo->setItemText(1, QCoreApplication::translate("gridPropertiesWidget", "Dots", nullptr));

        label_4->setText(QCoreApplication::translate("gridPropertiesWidget", "Visualization", nullptr));
        label_3->setText(QCoreApplication::translate("gridPropertiesWidget", "Line Thickness", nullptr));
        label_17->setText(QCoreApplication::translate("gridPropertiesWidget", "X Axis", nullptr));
        label_18->setText(QCoreApplication::translate("gridPropertiesWidget", "Z Axis", nullptr));
        xAxisColorButton->setText(QCoreApplication::translate("gridPropertiesWidget", "Change X Axis Color", nullptr));
        zAxisColorButton->setText(QCoreApplication::translate("gridPropertiesWidget", "Change Z Axis Color", nullptr));
        masterVisibilityCheck->setText(QCoreApplication::translate("gridPropertiesWidget", "Master Visibility", nullptr));
        gridSnapToggleButton->setText(QCoreApplication::translate("gridPropertiesWidget", "Toggle Grid Snapping", nullptr));
        label_2->setText(QCoreApplication::translate("gridPropertiesWidget", "Other Controls", nullptr));
    } // retranslateUi

};

namespace Ui {
    class gridPropertiesWidget: public Ui_gridPropertiesWidget {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_GRIDPROPERTIESWIDGET_H
