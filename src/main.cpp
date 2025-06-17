#include <QApplication>
#include <QSurfaceFormat>
#include <QLoggingCategory>

static void qtMessageOutput(QtMsgType type, const QMessageLogContext &context, const QString &msg)
{
    QByteArray localMsg = msg.toLocal8Bit();
    const char *file = context.file ? context.file : "";
    const char *function = context.function ? context.function : "";
    switch (type) {
    case QtDebugMsg:
        fprintf(stderr, "Debug: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        break;
    case QtInfoMsg:
        fprintf(stderr, "Info: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        break;
    case QtWarningMsg:
        fprintf(stderr, "Warning: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        break;
    case QtCriticalMsg:
        fprintf(stderr, "Critical: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        break;
    case QtFatalMsg:
        fprintf(stderr, "Fatal: %s (%s:%u, %s)\n", localMsg.constData(), file, context.line, function);
        abort();
    }
    fflush(stderr);
}
#include "MainWindow.hpp"

int main(int argc, char* argv[])
{
    qInstallMessageHandler(qtMessageOutput);
    QApplication app(argc, argv);

    // Enable verbose Qt logging for OpenGL and platform issues
    QLoggingCategory::setFilterRules(QStringLiteral("qt.qpa.*=true\nqt.opengl.*=true"));

    // --- Set the default OpenGL format for the entire application ---
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setVersion(4, 1); // Or your target OpenGL version
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSamples(4); // Request a Core Profile context
    format.setOption(QSurfaceFormat::StereoBuffers, true);
    format.setOption(QSurfaceFormat::DebugContext);
    QSurfaceFormat::setDefaultFormat(format);
    // ----------------------------------------------------------------

    MainWindow mainWindow;
    mainWindow.show();
    return app.exec();
}
