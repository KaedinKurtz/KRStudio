/********************************************************************************
** Form generated from reading UI file 'mainwindow.ui'
**
** Created by: Qt User Interface Compiler version 6.9.0
**
** WARNING! All changes made in this file will be lost when recompiling UI file!
********************************************************************************/

#ifndef UI_MAINWINDOW_H
#define UI_MAINWINDOW_H

#include <QtCore/QVariant>
#include <QtGui/QAction>
#include <QtWidgets/QApplication>
#include <QtWidgets/QMainWindow>
#include <QtWidgets/QMenu>
#include <QtWidgets/QMenuBar>
#include <QtWidgets/QStatusBar>
#include <QtWidgets/QToolBar>
#include <QtWidgets/QWidget>

QT_BEGIN_NAMESPACE

class Ui_MainWindow
{
public:
    QAction *actionExit;
    QAction *actionSaveState;
    QAction *actionRestoreState;
    QWidget *centralWidget;
    QStatusBar *statusBar;
    QMenuBar *menuBar;
    QMenu *menuFile;
    QMenu *menuView;
    QMenu *menuAbout;
    QMenu *menuTests;
    QToolBar *toolBar;

    void setupUi(QMainWindow *MainWindow)
    {
        if (MainWindow->objectName().isEmpty())
            MainWindow->setObjectName("MainWindow");
        MainWindow->resize(400, 300);
        MainWindow->setDockOptions(QMainWindow::AllowTabbedDocks);
        actionExit = new QAction(MainWindow);
        actionExit->setObjectName("actionExit");
        actionSaveState = new QAction(MainWindow);
        actionSaveState->setObjectName("actionSaveState");
        actionRestoreState = new QAction(MainWindow);
        actionRestoreState->setObjectName("actionRestoreState");
        centralWidget = new QWidget(MainWindow);
        centralWidget->setObjectName("centralWidget");
        MainWindow->setCentralWidget(centralWidget);
        statusBar = new QStatusBar(MainWindow);
        statusBar->setObjectName("statusBar");
        MainWindow->setStatusBar(statusBar);
        menuBar = new QMenuBar(MainWindow);
        menuBar->setObjectName("menuBar");
        menuBar->setGeometry(QRect(0, 0, 400, 21));
        menuFile = new QMenu(menuBar);
        menuFile->setObjectName("menuFile");
        menuView = new QMenu(menuBar);
        menuView->setObjectName("menuView");
        menuAbout = new QMenu(menuBar);
        menuAbout->setObjectName("menuAbout");
        menuTests = new QMenu(menuBar);
        menuTests->setObjectName("menuTests");
        MainWindow->setMenuBar(menuBar);
        toolBar = new QToolBar(MainWindow);
        toolBar->setObjectName("toolBar");
        MainWindow->addToolBar(Qt::ToolBarArea::TopToolBarArea, toolBar);

        menuBar->addAction(menuFile->menuAction());
        menuBar->addAction(menuView->menuAction());
        menuBar->addAction(menuTests->menuAction());
        menuBar->addAction(menuAbout->menuAction());
        menuFile->addAction(actionSaveState);
        menuFile->addAction(actionRestoreState);

        retranslateUi(MainWindow);

        QMetaObject::connectSlotsByName(MainWindow);
    } // setupUi

    void retranslateUi(QMainWindow *MainWindow)
    {
        MainWindow->setWindowTitle(QCoreApplication::translate("MainWindow", "MainWindow", nullptr));
        actionExit->setText(QCoreApplication::translate("MainWindow", "Exit", nullptr));
        actionSaveState->setText(QCoreApplication::translate("MainWindow", "Save State", nullptr));
        actionRestoreState->setText(QCoreApplication::translate("MainWindow", "Restore State", nullptr));
        menuFile->setTitle(QCoreApplication::translate("MainWindow", "File", nullptr));
        menuView->setTitle(QCoreApplication::translate("MainWindow", "View", nullptr));
        menuAbout->setTitle(QCoreApplication::translate("MainWindow", "About", nullptr));
        menuTests->setTitle(QCoreApplication::translate("MainWindow", "Tests", nullptr));
        toolBar->setWindowTitle(QCoreApplication::translate("MainWindow", "toolBar", nullptr));
    } // retranslateUi

};

namespace Ui {
    class MainWindow: public Ui_MainWindow {};
} // namespace Ui

QT_END_NAMESPACE

#endif // UI_MAINWINDOW_H
