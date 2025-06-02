#include <QApplication>
#include <QFile>
#include "mainwindow.h"

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);

    // Load the stylesheet
    QFile file(":/styles/dark_style.qss"); // Assuming you added it to your .qrc file
    file.open(QFile::ReadOnly | QFile::Text);
    QString styleSheet = QLatin1String(file.readAll());

    // Apply the stylesheet to the entire application
    a.setStyleSheet(styleSheet);

    MainWindow w;
    w.show();
    return a.exec();
}
