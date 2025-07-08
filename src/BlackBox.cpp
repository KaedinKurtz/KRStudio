#include "BlackBox.hpp"
#include "RenderingSystem.hpp"
#include "ViewportWidget.hpp" // We need the full definition for casting

#include <QTextStream>
#include <QOpenGLContext>
#include <QDateTime>

using dbg::BlackBox;

// Helper to format GLuint IDs for printing.
static QString fmt(GLuint id) {
    return id ? QStringLiteral("0x%1").arg(id, 0, 16) : QStringLiteral("null");
}

// --- Singleton Plumbing ---
BlackBox& BlackBox::instance() {
    static BlackBox s;
    return s;
}

BlackBox::BlackBox() : m_file(QStringLiteral("RS_DebugDump.txt")) {
    // Open the file in append mode.
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        qWarning() << "Could not open BlackBox debug dump file for writing.";
    }
}

// --- Public API ---
void BlackBox::dumpState(const QString& tag,
    const RenderingSystem& rs,
    QOpenGLWidget* vp,
    QOpenGLFunctions_4_3_Core* gl)
{
    if (!m_file.isOpen() || !vp) return;

    QTextStream out(&m_file);
    out << "============================================================\n";
    out << "[ " << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss.zzz")
        << " ]  " << tag << '\n';

    // --- Viewport / Context Info ---
    out << "    Viewport ptr: " << vp
        << "  QWidget winId: 0x" << QString::number(quintptr(vp->winId()), 16) << '\n';
    out << "    Context ptr : " << vp->context() << '\n';

    // --- Per-Viewport FBOs ---
    // FIX: Cast the QOpenGLWidget* to the ViewportWidget* that the map expects.
    ViewportWidget* viewport = qobject_cast<ViewportWidget*>(vp);
    if (viewport && rs.m_targets.contains(viewport)) {
        // FIX: Use .value() to access the FBO struct from the QMap.
        const TargetFBOs& t = rs.m_targets.value(viewport);
        out << "    FBOs:\n"
            << "        mainFBO        : " << fmt(t.mainFBO) << '\n'
            << "        glowFBO        : " << fmt(t.glowFBO) << '\n'
            << "        pingpongFBO[0] : " << fmt(t.pingpongFBO[0]) << '\n'
            << "        pingpongFBO[1] : " << fmt(t.pingpongFBO[1]) << '\n';
    }
    else {
        out << "    (viewport not in m_targets map)\n";
    }

    // --- DELETED: VAOs / VBOs section ---
    // This entire section is removed because the `m_contextPrimitives` member
    // no longer exists in RenderingSystem. This data is now managed by the
    // individual RenderPass classes and is no longer accessible here.

    // --- Live GL State ---
    GLint drawFBO = 0, sRGB = 0;
    if (gl) {
        gl->glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &drawFBO);
        gl->glGetIntegerv(GL_FRAMEBUFFER_SRGB, &sRGB);
    }
    out << "    GL: drawFBO=" << fmt(drawFBO)
        << "  SRGB=" << (sRGB ? "ON" : "OFF") << '\n';

    out.flush();
}