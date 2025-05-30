#include <QApplication>
#include <QSurfaceFormat> // <-- Include this header
#include "MainWindow.hpp"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // --- Set the default OpenGL format for the entire application ---
    QSurfaceFormat format;
    format.setDepthBufferSize(24); // Request a 24-bit depth buffer
    format.setVersion(3, 3);       // Request OpenGL 3.3
    format.setProfile(QSurfaceFormat::CoreProfile); // Request a Core Profile context
    QSurfaceFormat::setDefaultFormat(format);
    // ----------------------------------------------------------------

    MainWindow mainWindow;
    mainWindow.show();
    return app.exec();
}