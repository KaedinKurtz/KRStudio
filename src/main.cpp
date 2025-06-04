#include <QApplication>
#include <QSurfaceFormat> // <-- Include this header
#include "MainWindow.hpp"

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    // --- Set the default OpenGL format for the entire application ---
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setVersion(3, 3); // Or your target OpenGL version
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSamples(4); // Request a Core Profile context
    format.setOption(QSurfaceFormat::StereoBuffers, true);
    QSurfaceFormat::setDefaultFormat(format);
    // ----------------------------------------------------------------

    MainWindow mainWindow;
    mainWindow.show();
    return app.exec();
}