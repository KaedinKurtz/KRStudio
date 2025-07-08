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
    // --- CORRECT INITIALIZATION ORDER ---

    // 1. Set up any custom message handlers first.
    qInstallMessageHandler(qtMessageOutput);

    // 2. Set application-wide attributes like context sharing.
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);

    // 3. Define and set the default OpenGL format for the ENTIRE application.
    QSurfaceFormat format;
    format.setDepthBufferSize(24);
    format.setStencilBufferSize(8);
    format.setVersion(4, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSamples(4);
    format.setOption(QSurfaceFormat::DebugContext);
    format.setColorSpace(QSurfaceFormat::sRGBColorSpace);
    QSurfaceFormat::setDefaultFormat(format);

    // 4. NOW, create the QApplication object. It will use the settings above.
    QApplication app(argc, argv);

    // 5. Configure any other app-specific systems like logging.
    QLoggingCategory::setFilterRules(QStringLiteral("qt.qpa.*=true\nqt.opengl.*=true"));

    // 6. Create and show your main window.
    MainWindow mainWindow;
    mainWindow.show();

    return app.exec();
}
