#pragma once

#include <QWidget>
#include "ui_gridPropertiesWidget.h"

class gridPropertiesWidget : public QWidget
{
	Q_OBJECT

public:
	gridPropertiesWidget(QWidget *parent = nullptr);
	~gridPropertiesWidget();

private:
	Ui::gridPropertiesWidgetClass ui;
};

