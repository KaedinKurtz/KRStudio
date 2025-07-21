/********************************************************************************
** Form generated from reading UI file 'RealSenseConfigMenu.ui'
**
** Created by: Qt User Interface Compiler version 6.8.3
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_REALSENSECONFIGMENU_H
#define UI_REALSENSECONFIGMENU_H

#include <QtCore/QVariant>
#include <QtWidgets/QApplication>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDoubleSpinBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QFrame>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QHeaderView>
#include <QtWidgets/QLabel>
#include <QtWidgets/QListView>
#include <QtWidgets/QSlider>
#include <QtWidgets/QTableWidget>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_RealSenseConfigMenu
{
public:
    QGridLayout *gridLayout_2;
    QGridLayout *gridLayout;
    QFrame *line;
    QLabel *label;
    QFrame *line_5;
    QWidget *widget_2;
    QGridLayout *gridLayout_3;
    QComboBox *RealSenseCameraSelectionComboBox;
    QToolButton *refreshRealSenseDevicesButton;
    QGroupBox *groupBox_2;
    QGridLayout *gridLayout_6;
    QGroupBox *groupBox_3;
    QGridLayout *gridLayout_7;
    QWidget *widget_4;
    QFormLayout *formLayout;
    QToolButton *enableDepthStream_toggle_tool;
    QToolButton *enableRGBStream_toggle_tool;
    QToolButton *enableInfaredStream_toggle_tool;
    QLabel *label_2;
    QComboBox *RSResolutionCombo;
    QLabel *label_3;
    QComboBox *RSFrameRateCombo;
    QLabel *label_4;
    QComboBox *RSPreviewsourceComboBox;
    QWidget *widget_3;
    QGridLayout *gridLayout_9;
    QLabel *label_6;
    QToolButton *RSAllignStreamsToggleTool;
    QSlider *RSExposureSlider;
    QDoubleSpinBox *RSWhiteBalanceSpinBox;
    QLabel *label_5;
    QLabel *label_7;
    QSlider *RSGainSlider;
    QToolButton *RSAutoExposureToggleTool;
    QDoubleSpinBox *RSGainSpinBox;
    QSlider *RSWhiteBalanceSlider;
    QDoubleSpinBox *RSExposureSpinBox;
    QWidget *widget_5;
    QHBoxLayout *horizontalLayout;
    QToolButton *RSTemporalFilterToggleTool;
    QToolButton *RSSpacialFilterToggleTool;
    QToolButton *startStreamingRSButton;
    QToolButton *stopStreamingRSButton;
    QGroupBox *groupBox_4;
    QGridLayout *gridLayout_8;
    QWidget *RSDeviceStreamPreview;
    QGroupBox *groupBox;
    QGridLayout *gridLayout_5;
    QListView *RealSenseDeviceList;
    QTableWidget *RSActiveDevicePropertiesList;

    void setupUi(QWidget *RealSenseConfigMenu)
    {
        if (RealSenseConfigMenu->objectName().isEmpty())
            RealSenseConfigMenu->setObjectName("RealSenseConfigMenu");
        RealSenseConfigMenu->resize(652, 927);
        gridLayout_2 = new QGridLayout(RealSenseConfigMenu);
        gridLayout_2->setObjectName("gridLayout_2");
        gridLayout_2->setContentsMargins(0, 0, 0, 0);
        gridLayout = new QGridLayout();
        gridLayout->setObjectName("gridLayout");
        line = new QFrame(RealSenseConfigMenu);
        line->setObjectName("line");
        line->setFrameShape(QFrame::Shape::HLine);
        line->setFrameShadow(QFrame::Shadow::Sunken);

        gridLayout->addWidget(line, 3, 0, 1, 1);

        label = new QLabel(RealSenseConfigMenu);
        label->setObjectName("label");
        label->setMinimumSize(QSize(650, 20));
        label->setMaximumSize(QSize(650, 20));
        label->setAlignment(Qt::AlignmentFlag::AlignCenter);

        gridLayout->addWidget(label, 0, 0, 1, 1);

        line_5 = new QFrame(RealSenseConfigMenu);
        line_5->setObjectName("line_5");
        line_5->setFrameShape(QFrame::Shape::HLine);
        line_5->setFrameShadow(QFrame::Shadow::Sunken);

        gridLayout->addWidget(line_5, 1, 0, 1, 1);

        widget_2 = new QWidget(RealSenseConfigMenu);
        widget_2->setObjectName("widget_2");
        QSizePolicy sizePolicy(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Expanding);
        sizePolicy.setHorizontalStretch(0);
        sizePolicy.setVerticalStretch(0);
        sizePolicy.setHeightForWidth(widget_2->sizePolicy().hasHeightForWidth());
        widget_2->setSizePolicy(sizePolicy);
        gridLayout_3 = new QGridLayout(widget_2);
        gridLayout_3->setObjectName("gridLayout_3");
        gridLayout_3->setVerticalSpacing(9);
        gridLayout_3->setContentsMargins(9, 0, 9, 0);
        RealSenseCameraSelectionComboBox = new QComboBox(widget_2);
        RealSenseCameraSelectionComboBox->setObjectName("RealSenseCameraSelectionComboBox");
        RealSenseCameraSelectionComboBox->setMinimumSize(QSize(0, 30));

        gridLayout_3->addWidget(RealSenseCameraSelectionComboBox, 3, 0, 1, 1);

        refreshRealSenseDevicesButton = new QToolButton(widget_2);
        refreshRealSenseDevicesButton->setObjectName("refreshRealSenseDevicesButton");
        QSizePolicy sizePolicy1(QSizePolicy::Policy::Preferred, QSizePolicy::Policy::Fixed);
        sizePolicy1.setHorizontalStretch(0);
        sizePolicy1.setVerticalStretch(0);
        sizePolicy1.setHeightForWidth(refreshRealSenseDevicesButton->sizePolicy().hasHeightForWidth());
        refreshRealSenseDevicesButton->setSizePolicy(sizePolicy1);
        refreshRealSenseDevicesButton->setMinimumSize(QSize(150, 40));

        gridLayout_3->addWidget(refreshRealSenseDevicesButton, 0, 0, 1, 1);

        groupBox_2 = new QGroupBox(widget_2);
        groupBox_2->setObjectName("groupBox_2");
        groupBox_2->setAlignment(Qt::AlignmentFlag::AlignCenter);
        gridLayout_6 = new QGridLayout(groupBox_2);
        gridLayout_6->setObjectName("gridLayout_6");
        gridLayout_6->setContentsMargins(0, 6, 0, 0);
        groupBox_3 = new QGroupBox(groupBox_2);
        groupBox_3->setObjectName("groupBox_3");
        sizePolicy.setHeightForWidth(groupBox_3->sizePolicy().hasHeightForWidth());
        groupBox_3->setSizePolicy(sizePolicy);
        groupBox_3->setAlignment(Qt::AlignmentFlag::AlignCenter);
        gridLayout_7 = new QGridLayout(groupBox_3);
        gridLayout_7->setSpacing(0);
        gridLayout_7->setObjectName("gridLayout_7");
        gridLayout_7->setContentsMargins(0, 6, 0, 0);
        widget_4 = new QWidget(groupBox_3);
        widget_4->setObjectName("widget_4");
        QSizePolicy sizePolicy2(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Preferred);
        sizePolicy2.setHorizontalStretch(0);
        sizePolicy2.setVerticalStretch(0);
        sizePolicy2.setHeightForWidth(widget_4->sizePolicy().hasHeightForWidth());
        widget_4->setSizePolicy(sizePolicy2);
        formLayout = new QFormLayout(widget_4);
        formLayout->setObjectName("formLayout");
        enableDepthStream_toggle_tool = new QToolButton(widget_4);
        enableDepthStream_toggle_tool->setObjectName("enableDepthStream_toggle_tool");
        QSizePolicy sizePolicy3(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Fixed);
        sizePolicy3.setHorizontalStretch(0);
        sizePolicy3.setVerticalStretch(0);
        sizePolicy3.setHeightForWidth(enableDepthStream_toggle_tool->sizePolicy().hasHeightForWidth());
        enableDepthStream_toggle_tool->setSizePolicy(sizePolicy3);

        formLayout->setWidget(0, QFormLayout::SpanningRole, enableDepthStream_toggle_tool);

        enableRGBStream_toggle_tool = new QToolButton(widget_4);
        enableRGBStream_toggle_tool->setObjectName("enableRGBStream_toggle_tool");
        sizePolicy3.setHeightForWidth(enableRGBStream_toggle_tool->sizePolicy().hasHeightForWidth());
        enableRGBStream_toggle_tool->setSizePolicy(sizePolicy3);

        formLayout->setWidget(1, QFormLayout::SpanningRole, enableRGBStream_toggle_tool);

        enableInfaredStream_toggle_tool = new QToolButton(widget_4);
        enableInfaredStream_toggle_tool->setObjectName("enableInfaredStream_toggle_tool");
        sizePolicy3.setHeightForWidth(enableInfaredStream_toggle_tool->sizePolicy().hasHeightForWidth());
        enableInfaredStream_toggle_tool->setSizePolicy(sizePolicy3);

        formLayout->setWidget(2, QFormLayout::SpanningRole, enableInfaredStream_toggle_tool);

        label_2 = new QLabel(widget_4);
        label_2->setObjectName("label_2");

        formLayout->setWidget(3, QFormLayout::LabelRole, label_2);

        RSResolutionCombo = new QComboBox(widget_4);
        RSResolutionCombo->addItem(QString());
        RSResolutionCombo->addItem(QString());
        RSResolutionCombo->addItem(QString());
        RSResolutionCombo->addItem(QString());
        RSResolutionCombo->addItem(QString());
        RSResolutionCombo->setObjectName("RSResolutionCombo");

        formLayout->setWidget(3, QFormLayout::FieldRole, RSResolutionCombo);

        label_3 = new QLabel(widget_4);
        label_3->setObjectName("label_3");

        formLayout->setWidget(4, QFormLayout::LabelRole, label_3);

        RSFrameRateCombo = new QComboBox(widget_4);
        RSFrameRateCombo->addItem(QString());
        RSFrameRateCombo->addItem(QString());
        RSFrameRateCombo->addItem(QString());
        RSFrameRateCombo->setObjectName("RSFrameRateCombo");

        formLayout->setWidget(4, QFormLayout::FieldRole, RSFrameRateCombo);

        label_4 = new QLabel(widget_4);
        label_4->setObjectName("label_4");

        formLayout->setWidget(5, QFormLayout::LabelRole, label_4);

        RSPreviewsourceComboBox = new QComboBox(widget_4);
        RSPreviewsourceComboBox->setObjectName("RSPreviewsourceComboBox");

        formLayout->setWidget(5, QFormLayout::FieldRole, RSPreviewsourceComboBox);


        gridLayout_7->addWidget(widget_4, 0, 0, 1, 1);

        widget_3 = new QWidget(groupBox_3);
        widget_3->setObjectName("widget_3");
        sizePolicy2.setHeightForWidth(widget_3->sizePolicy().hasHeightForWidth());
        widget_3->setSizePolicy(sizePolicy2);
        gridLayout_9 = new QGridLayout(widget_3);
        gridLayout_9->setObjectName("gridLayout_9");
        label_6 = new QLabel(widget_3);
        label_6->setObjectName("label_6");

        gridLayout_9->addWidget(label_6, 1, 0, 1, 1);

        RSAllignStreamsToggleTool = new QToolButton(widget_3);
        RSAllignStreamsToggleTool->setObjectName("RSAllignStreamsToggleTool");
        sizePolicy3.setHeightForWidth(RSAllignStreamsToggleTool->sizePolicy().hasHeightForWidth());
        RSAllignStreamsToggleTool->setSizePolicy(sizePolicy3);

        gridLayout_9->addWidget(RSAllignStreamsToggleTool, 4, 0, 1, 3);

        RSExposureSlider = new QSlider(widget_3);
        RSExposureSlider->setObjectName("RSExposureSlider");
        RSExposureSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout_9->addWidget(RSExposureSlider, 0, 1, 1, 1);

        RSWhiteBalanceSpinBox = new QDoubleSpinBox(widget_3);
        RSWhiteBalanceSpinBox->setObjectName("RSWhiteBalanceSpinBox");

        gridLayout_9->addWidget(RSWhiteBalanceSpinBox, 2, 2, 1, 1);

        label_5 = new QLabel(widget_3);
        label_5->setObjectName("label_5");

        gridLayout_9->addWidget(label_5, 0, 0, 1, 1);

        label_7 = new QLabel(widget_3);
        label_7->setObjectName("label_7");

        gridLayout_9->addWidget(label_7, 2, 0, 1, 1);

        RSGainSlider = new QSlider(widget_3);
        RSGainSlider->setObjectName("RSGainSlider");
        RSGainSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout_9->addWidget(RSGainSlider, 1, 1, 1, 1);

        RSAutoExposureToggleTool = new QToolButton(widget_3);
        RSAutoExposureToggleTool->setObjectName("RSAutoExposureToggleTool");
        sizePolicy3.setHeightForWidth(RSAutoExposureToggleTool->sizePolicy().hasHeightForWidth());
        RSAutoExposureToggleTool->setSizePolicy(sizePolicy3);

        gridLayout_9->addWidget(RSAutoExposureToggleTool, 3, 0, 1, 3);

        RSGainSpinBox = new QDoubleSpinBox(widget_3);
        RSGainSpinBox->setObjectName("RSGainSpinBox");

        gridLayout_9->addWidget(RSGainSpinBox, 1, 2, 1, 1);

        RSWhiteBalanceSlider = new QSlider(widget_3);
        RSWhiteBalanceSlider->setObjectName("RSWhiteBalanceSlider");
        RSWhiteBalanceSlider->setOrientation(Qt::Orientation::Horizontal);

        gridLayout_9->addWidget(RSWhiteBalanceSlider, 2, 1, 1, 1);

        RSExposureSpinBox = new QDoubleSpinBox(widget_3);
        RSExposureSpinBox->setObjectName("RSExposureSpinBox");

        gridLayout_9->addWidget(RSExposureSpinBox, 0, 2, 1, 1);

        widget_5 = new QWidget(widget_3);
        widget_5->setObjectName("widget_5");
        horizontalLayout = new QHBoxLayout(widget_5);
        horizontalLayout->setObjectName("horizontalLayout");
        horizontalLayout->setContentsMargins(0, 0, 0, 0);
        RSTemporalFilterToggleTool = new QToolButton(widget_5);
        RSTemporalFilterToggleTool->setObjectName("RSTemporalFilterToggleTool");
        sizePolicy3.setHeightForWidth(RSTemporalFilterToggleTool->sizePolicy().hasHeightForWidth());
        RSTemporalFilterToggleTool->setSizePolicy(sizePolicy3);

        horizontalLayout->addWidget(RSTemporalFilterToggleTool);

        RSSpacialFilterToggleTool = new QToolButton(widget_5);
        RSSpacialFilterToggleTool->setObjectName("RSSpacialFilterToggleTool");
        sizePolicy3.setHeightForWidth(RSSpacialFilterToggleTool->sizePolicy().hasHeightForWidth());
        RSSpacialFilterToggleTool->setSizePolicy(sizePolicy3);

        horizontalLayout->addWidget(RSSpacialFilterToggleTool);


        gridLayout_9->addWidget(widget_5, 5, 0, 1, 3);


        gridLayout_7->addWidget(widget_3, 0, 1, 1, 1);


        gridLayout_6->addWidget(groupBox_3, 1, 0, 1, 2);

        startStreamingRSButton = new QToolButton(groupBox_2);
        startStreamingRSButton->setObjectName("startStreamingRSButton");
        sizePolicy3.setHeightForWidth(startStreamingRSButton->sizePolicy().hasHeightForWidth());
        startStreamingRSButton->setSizePolicy(sizePolicy3);
        startStreamingRSButton->setMinimumSize(QSize(0, 40));

        gridLayout_6->addWidget(startStreamingRSButton, 0, 0, 1, 1);

        stopStreamingRSButton = new QToolButton(groupBox_2);
        stopStreamingRSButton->setObjectName("stopStreamingRSButton");
        sizePolicy3.setHeightForWidth(stopStreamingRSButton->sizePolicy().hasHeightForWidth());
        stopStreamingRSButton->setSizePolicy(sizePolicy3);
        stopStreamingRSButton->setMinimumSize(QSize(0, 40));

        gridLayout_6->addWidget(stopStreamingRSButton, 0, 1, 1, 1);

        groupBox_4 = new QGroupBox(groupBox_2);
        groupBox_4->setObjectName("groupBox_4");
        groupBox_4->setAlignment(Qt::AlignmentFlag::AlignCenter);
        gridLayout_8 = new QGridLayout(groupBox_4);
        gridLayout_8->setObjectName("gridLayout_8");
        gridLayout_8->setContentsMargins(0, 9, 0, 0);
        RSDeviceStreamPreview = new QWidget(groupBox_4);
        RSDeviceStreamPreview->setObjectName("RSDeviceStreamPreview");
        QSizePolicy sizePolicy4(QSizePolicy::Policy::Expanding, QSizePolicy::Policy::Expanding);
        sizePolicy4.setHorizontalStretch(0);
        sizePolicy4.setVerticalStretch(0);
        sizePolicy4.setHeightForWidth(RSDeviceStreamPreview->sizePolicy().hasHeightForWidth());
        RSDeviceStreamPreview->setSizePolicy(sizePolicy4);
        RSDeviceStreamPreview->setMinimumSize(QSize(0, 200));

        gridLayout_8->addWidget(RSDeviceStreamPreview, 0, 0, 1, 1);


        gridLayout_6->addWidget(groupBox_4, 2, 0, 1, 2);


        gridLayout_3->addWidget(groupBox_2, 4, 0, 1, 1);

        groupBox = new QGroupBox(widget_2);
        groupBox->setObjectName("groupBox");
        groupBox->setAlignment(Qt::AlignmentFlag::AlignCenter);
        gridLayout_5 = new QGridLayout(groupBox);
        gridLayout_5->setObjectName("gridLayout_5");
        gridLayout_5->setContentsMargins(0, 6, 0, 0);
        RealSenseDeviceList = new QListView(groupBox);
        RealSenseDeviceList->setObjectName("RealSenseDeviceList");
        sizePolicy4.setHeightForWidth(RealSenseDeviceList->sizePolicy().hasHeightForWidth());
        RealSenseDeviceList->setSizePolicy(sizePolicy4);
        RealSenseDeviceList->setMinimumSize(QSize(0, 18));
        RealSenseDeviceList->setMaximumSize(QSize(16777215, 300));

        gridLayout_5->addWidget(RealSenseDeviceList, 0, 0, 1, 1);

        RSActiveDevicePropertiesList = new QTableWidget(groupBox);
        if (RSActiveDevicePropertiesList->columnCount() < 1)
            RSActiveDevicePropertiesList->setColumnCount(1);
        QTableWidgetItem *__qtablewidgetitem = new QTableWidgetItem();
        RSActiveDevicePropertiesList->setHorizontalHeaderItem(0, __qtablewidgetitem);
        if (RSActiveDevicePropertiesList->rowCount() < 7)
            RSActiveDevicePropertiesList->setRowCount(7);
        QTableWidgetItem *__qtablewidgetitem1 = new QTableWidgetItem();
        RSActiveDevicePropertiesList->setVerticalHeaderItem(0, __qtablewidgetitem1);
        QTableWidgetItem *__qtablewidgetitem2 = new QTableWidgetItem();
        RSActiveDevicePropertiesList->setVerticalHeaderItem(1, __qtablewidgetitem2);
        QTableWidgetItem *__qtablewidgetitem3 = new QTableWidgetItem();
        RSActiveDevicePropertiesList->setVerticalHeaderItem(2, __qtablewidgetitem3);
        QTableWidgetItem *__qtablewidgetitem4 = new QTableWidgetItem();
        RSActiveDevicePropertiesList->setVerticalHeaderItem(3, __qtablewidgetitem4);
        QTableWidgetItem *__qtablewidgetitem5 = new QTableWidgetItem();
        RSActiveDevicePropertiesList->setVerticalHeaderItem(4, __qtablewidgetitem5);
        QTableWidgetItem *__qtablewidgetitem6 = new QTableWidgetItem();
        RSActiveDevicePropertiesList->setVerticalHeaderItem(5, __qtablewidgetitem6);
        QTableWidgetItem *__qtablewidgetitem7 = new QTableWidgetItem();
        RSActiveDevicePropertiesList->setVerticalHeaderItem(6, __qtablewidgetitem7);
        RSActiveDevicePropertiesList->setObjectName("RSActiveDevicePropertiesList");
        sizePolicy4.setHeightForWidth(RSActiveDevicePropertiesList->sizePolicy().hasHeightForWidth());
        RSActiveDevicePropertiesList->setSizePolicy(sizePolicy4);
        RSActiveDevicePropertiesList->setMinimumSize(QSize(0, 100));
        RSActiveDevicePropertiesList->setMaximumSize(QSize(16777215, 300));
        RSActiveDevicePropertiesList->horizontalHeader()->setCascadingSectionResizes(false);
        RSActiveDevicePropertiesList->horizontalHeader()->setProperty("showSortIndicator", QVariant(false));
        RSActiveDevicePropertiesList->horizontalHeader()->setStretchLastSection(true);
        RSActiveDevicePropertiesList->verticalHeader()->setCascadingSectionResizes(false);
        RSActiveDevicePropertiesList->verticalHeader()->setMinimumSectionSize(10);
        RSActiveDevicePropertiesList->verticalHeader()->setDefaultSectionSize(22);

        gridLayout_5->addWidget(RSActiveDevicePropertiesList, 0, 1, 1, 1);


        gridLayout_3->addWidget(groupBox, 1, 0, 1, 1);


        gridLayout->addWidget(widget_2, 2, 0, 1, 1);


        gridLayout_2->addLayout(gridLayout, 0, 0, 1, 1);


        retranslateUi(RealSenseConfigMenu);

        QMetaObject::connectSlotsByName(RealSenseConfigMenu);
    } // setupUi

    void retranslateUi(QWidget *RealSenseConfigMenu)
    {
        RealSenseConfigMenu->setWindowTitle(QCoreApplication::translate("RealSenseConfigMenu", "Form", nullptr));
        label->setText(QCoreApplication::translate("RealSenseConfigMenu", "Connection Management", nullptr));
        refreshRealSenseDevicesButton->setText(QCoreApplication::translate("RealSenseConfigMenu", "Refresh RealSense Devices", nullptr));
        groupBox_2->setTitle(QCoreApplication::translate("RealSenseConfigMenu", "Streaming Controls", nullptr));
        groupBox_3->setTitle(QCoreApplication::translate("RealSenseConfigMenu", "Stream Configuration", nullptr));
        enableDepthStream_toggle_tool->setText(QCoreApplication::translate("RealSenseConfigMenu", "Enable Depth Stream", nullptr));
        enableRGBStream_toggle_tool->setText(QCoreApplication::translate("RealSenseConfigMenu", "Enable Color Stream", nullptr));
        enableInfaredStream_toggle_tool->setText(QCoreApplication::translate("RealSenseConfigMenu", "Enable Infared Stream", nullptr));
        label_2->setText(QCoreApplication::translate("RealSenseConfigMenu", "Resolution", nullptr));
        RSResolutionCombo->setItemText(0, QCoreApplication::translate("RealSenseConfigMenu", "640 x 480", nullptr));
        RSResolutionCombo->setItemText(1, QCoreApplication::translate("RealSenseConfigMenu", "1280 x 720", nullptr));
        RSResolutionCombo->setItemText(2, QCoreApplication::translate("RealSenseConfigMenu", "1920 x 1080", nullptr));
        RSResolutionCombo->setItemText(3, QCoreApplication::translate("RealSenseConfigMenu", "2560 x 1440", nullptr));
        RSResolutionCombo->setItemText(4, QCoreApplication::translate("RealSenseConfigMenu", "3940 x 2160", nullptr));

        label_3->setText(QCoreApplication::translate("RealSenseConfigMenu", "Framerate", nullptr));
        RSFrameRateCombo->setItemText(0, QCoreApplication::translate("RealSenseConfigMenu", "15 FPS", nullptr));
        RSFrameRateCombo->setItemText(1, QCoreApplication::translate("RealSenseConfigMenu", "30 FPS", nullptr));
        RSFrameRateCombo->setItemText(2, QCoreApplication::translate("RealSenseConfigMenu", "60 FPS", nullptr));

        label_4->setText(QCoreApplication::translate("RealSenseConfigMenu", "Preview Source", nullptr));
        label_6->setText(QCoreApplication::translate("RealSenseConfigMenu", "Gain", nullptr));
        RSAllignStreamsToggleTool->setText(QCoreApplication::translate("RealSenseConfigMenu", "Allign Streams", nullptr));
        label_5->setText(QCoreApplication::translate("RealSenseConfigMenu", "Exposure", nullptr));
        label_7->setText(QCoreApplication::translate("RealSenseConfigMenu", "White Balance", nullptr));
        RSAutoExposureToggleTool->setText(QCoreApplication::translate("RealSenseConfigMenu", "Auto Exposure", nullptr));
        RSTemporalFilterToggleTool->setText(QCoreApplication::translate("RealSenseConfigMenu", "Temporal Filter", nullptr));
        RSSpacialFilterToggleTool->setText(QCoreApplication::translate("RealSenseConfigMenu", "Spacial Filter", nullptr));
        startStreamingRSButton->setText(QCoreApplication::translate("RealSenseConfigMenu", "Start Streaming", nullptr));
        stopStreamingRSButton->setText(QCoreApplication::translate("RealSenseConfigMenu", "Stop Streaming", nullptr));
        groupBox_4->setTitle(QCoreApplication::translate("RealSenseConfigMenu", "Stream Preview", nullptr));
        groupBox->setTitle(QCoreApplication::translate("RealSenseConfigMenu", "Available RealSense Devices", nullptr));
        QTableWidgetItem *___qtablewidgetitem = RSActiveDevicePropertiesList->horizontalHeaderItem(0);
        ___qtablewidgetitem->setText(QCoreApplication::translate("RealSenseConfigMenu", "Active Device Value", nullptr));
        QTableWidgetItem *___qtablewidgetitem1 = RSActiveDevicePropertiesList->verticalHeaderItem(0);
        ___qtablewidgetitem1->setText(QCoreApplication::translate("RealSenseConfigMenu", "Model Name", nullptr));
        QTableWidgetItem *___qtablewidgetitem2 = RSActiveDevicePropertiesList->verticalHeaderItem(1);
        ___qtablewidgetitem2->setText(QCoreApplication::translate("RealSenseConfigMenu", "Serial Number", nullptr));
        QTableWidgetItem *___qtablewidgetitem3 = RSActiveDevicePropertiesList->verticalHeaderItem(2);
        ___qtablewidgetitem3->setText(QCoreApplication::translate("RealSenseConfigMenu", "Firmware Version", nullptr));
        QTableWidgetItem *___qtablewidgetitem4 = RSActiveDevicePropertiesList->verticalHeaderItem(3);
        ___qtablewidgetitem4->setText(QCoreApplication::translate("RealSenseConfigMenu", "Product ID", nullptr));
        QTableWidgetItem *___qtablewidgetitem5 = RSActiveDevicePropertiesList->verticalHeaderItem(4);
        ___qtablewidgetitem5->setText(QCoreApplication::translate("RealSenseConfigMenu", "Camera Locked", nullptr));
        QTableWidgetItem *___qtablewidgetitem6 = RSActiveDevicePropertiesList->verticalHeaderItem(5);
        ___qtablewidgetitem6->setText(QCoreApplication::translate("RealSenseConfigMenu", "Advanced Mode", nullptr));
        QTableWidgetItem *___qtablewidgetitem7 = RSActiveDevicePropertiesList->verticalHeaderItem(6);
        ___qtablewidgetitem7->setText(QCoreApplication::translate("RealSenseConfigMenu", "Product Line", nullptr));
    } // retranslateUi

};

namespace Ui {
    class RealSenseConfigMenu: public Ui_RealSenseConfigMenu {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_REALSENSECONFIGMENU_H
