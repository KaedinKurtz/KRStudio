#include <QApplication>
#include <QSurfaceFormat>
#include <QCoreApplication>
#include <QDebug>
#include <QtWidgets>
#include "MainWindow.hpp"
#include "DatabaseManager.hpp"
#include "SettingsManager.hpp"
#include "Camera.hpp"
#include <glm/glm.hpp>
#include <cmath>

// custom message handler (you can leave this out if you don�t need it)
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
    f.setVersion(4, 5);
    f.setProfile(QSurfaceFormat::CoreProfile);
    f.setSwapBehavior(QSurfaceFormat::DoubleBuffer);
    f.setDepthBufferSize(24);
    f.setStencilBufferSize(8);
    // No MSAA: the deferred pipeline resolves into a single-sampled FBO and
    // blits it to the widget backbuffer — blitting into a multisampled
    // backbuffer is GL_INVALID_OPERATION and leaves the window blank.
    f.setSamples(0);
    f.setSwapInterval(1); // vsync — present at most once per display refresh

    return f;
}

int main(int argc, char* argv[])
{
    qInstallMessageHandler(qtMessageOutput);

    // Identify the app so QSettings() resolves to a stable per-user store
    // (HKCU\Software\KRStudio\KRStudio on Windows). Must be set before any
    // QSettings() is constructed (SettingsManager is lazy-init).
    QCoreApplication::setOrganizationName(QStringLiteral("KRStudio"));
    QCoreApplication::setApplicationName(QStringLiteral("KRStudio"));

    // share OpenGL contexts & set our default format
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QSurfaceFormat::setDefaultFormat(createDefaultFormat());

    QApplication app(argc, argv);

    // Headless settings gate (CI/dev): round-trip, persistence, clamp, defaults.
    if (qEnvironmentVariableIsSet("KRS_SETTINGS_SELFTEST")) {
        int fails = krs::SettingsManager::selfTest();
        // Decisive readback: the FOV setting must actually drive the projection
        // matrix. glm::perspective puts 1/tan(fovy/2) at P[1][1]. (Round-trip in
        // SettingsManager proves persistence; THIS proves the value reaches the
        // render math.)
        Camera::setFovDeg(60.0f);
        Camera cam;
        const float m11 = cam.getProjectionMatrix(1.0f)[1][1];
        const float expected = 1.0f / std::tan(glm::radians(60.0f) * 0.5f);
        const bool projOk = std::abs(m11 - expected) < 1e-3f;
        qInfo().noquote().nospace() << "[SETTINGS] camera FOV->projection "
                                    << (projOk ? "PASS" : "FAIL")
                                    << " (P[1][1]=" << m11 << " expected " << expected << ")";
        if (!projOk) ++fails;
        Camera::setFovDeg(45.0f); // restore default
        return fails == 0 ? 0 : 1;
    }
    // Test/override hook: KRS_SET_SETTING="key=value" persists+applies a setting at
    // boot (drives pixel-probe verification of any setting headlessly).
    if (qEnvironmentVariableIsSet("KRS_SET_SETTING")) {
        const QString kv = qEnvironmentVariable("KRS_SET_SETTING");
        const int eqi = kv.indexOf('=');
        if (eqi > 0) krs::SettingsManager::instance().setFromString(kv.left(eqi), kv.mid(eqi + 1));
    }

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
    try {
        MainWindow w;
        qInfo() << ">>> MainWindow constructed OK";
        w.show();
        return app.exec();
    }
    catch (const std::exception& e) {
        qCritical() << "MainWindow threw std::exception:" << e.what();
        return -1;
    }
    catch (...) {
        qCritical() << "MainWindow threw an unknown exception";
        return -1;
    }

    int result = app.exec();

    // cleanly shut down DB
    db::DatabaseManager::instance().shutdown();
    return result;
}
