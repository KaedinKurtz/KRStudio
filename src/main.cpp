#include <QApplication>
#include <QSurfaceFormat>
#include <QLoggingCategory>
#include <QOpenGLVersionFunctionsFactory>
#include <QOpenGLContext>
#include "MainWindow.hpp"


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


// ---------------------------------------------------------------------
// 1. Dummy share-root context – lives for the whole program
// ---------------------------------------------------------------------
static QOpenGLContext* gShareRoot = nullptr;

static void initShareRoot()
{
    if (gShareRoot)                    // idempotent
        return;

    gShareRoot = new QOpenGLContext;
    gShareRoot->setFormat(QSurfaceFormat::defaultFormat());
    gShareRoot->create();              // no surface needed
}

// ---------------------------------------------------------------------
// 2. Helper to set exactly one default GL format
// ---------------------------------------------------------------------
static QSurfaceFormat createDefaultFormat()
{
    QSurfaceFormat fmt;
    fmt.setRenderableType(QSurfaceFormat::OpenGL);
    fmt.setVersion(4, 3);
    fmt.setProfile(QSurfaceFormat::CoreProfile);
    fmt.setDepthBufferSize(24);
    fmt.setStencilBufferSize(8);
    fmt.setSamples(4);
    fmt.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    fmt.setColorSpace(QSurfaceFormat::sRGBColorSpace);
    // fmt.setOption(QSurfaceFormat::DebugContext); // if you want the debug ctx
    return fmt;
}

int main(int argc, char* argv[])
{
    // 1.  message handler, attributes, default format
    qInstallMessageHandler(qtMessageOutput);
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QSurfaceFormat::setDefaultFormat(createDefaultFormat());

    // 2.  *Now* we may create QApplication
    QApplication app(argc, argv);

    // 3.  Root context must wait until QPA is up
    initShareRoot();                       // <-- safe here

    // 4.  Usual startup
    MainWindow w;
    w.show();

    return app.exec();
}
