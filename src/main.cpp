#include <QApplication>
#include <QSurfaceFormat>
#include <QCoreApplication>
#include <QDebug>
#include <QtWidgets>
#include "MainWindow.hpp"
#include "DatabaseManager.hpp"

// custom message handler (you can leave this out if you don’t need it)
static void qtMessageOutput(QtMsgType type, const QMessageLogContext& ctx, const QString& msg)
{
    QByteArray local = msg.toLocal8Bit();
    fprintf(stderr, "%s\n", local.constData());
}

// one?time shared OpenGL context
static QOpenGLContext* gShareRoot = nullptr;
static void initShareRoot()
{
    if (!gShareRoot) {
        gShareRoot = new QOpenGLContext;
        gShareRoot->setFormat(QSurfaceFormat::defaultFormat());
        gShareRoot->create();
    }
}

// default format for all GL windows
static QSurfaceFormat createDefaultFormat()
{
    QSurfaceFormat f;
    f.setRenderableType(QSurfaceFormat::OpenGL);
    f.setVersion(4, 3);
    f.setProfile(QSurfaceFormat::CoreProfile);
    f.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    f.setDepthBufferSize(24);
    f.setStencilBufferSize(8);
    f.setSamples(4); // For MSAA

    return f;
}

int main(int argc, char* argv[])
{
    // get debug plugin output (optional)
    qputenv("QT_DEBUG_PLUGINS", QByteArrayLiteral("1"));
    qInstallMessageHandler(qtMessageOutput);

    // share OpenGL contexts & set our default format
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QSurfaceFormat::setDefaultFormat(createDefaultFormat());

    QApplication app(argc, argv);

    // make sure Qt loads the debug sqlite plugin from "./sqldrivers"
    QStringList paths = QCoreApplication::libraryPaths();
    paths.prepend(QCoreApplication::applicationDirPath() + "/sqldrivers");
    QCoreApplication::setLibraryPaths(paths);

    // initialize the database
    db::DatabaseConfig cfg;
    cfg.databasePath = "krobot_studio.db";
    if (!db::DatabaseManager::instance().initialize(cfg))
        return -1;

    // initialize our shared GL context
    initShareRoot();

    // launch main window
    MainWindow w;
    w.show();

    int result = app.exec();

    // cleanly shut down DB
    db::DatabaseManager::instance().shutdown();
    return result;
}
