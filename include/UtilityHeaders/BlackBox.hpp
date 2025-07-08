//  BlackBox.hpp
#pragma once
#include <QFile>
#include <QDateTime>
#include <QOpenGLFunctions_4_3_Core>
class QOpenGLWidget;
class RenderingSystem;                 // forward declaration

namespace dbg {

    class BlackBox
    {
    public:
        static BlackBox& instance();       // singleton access

        void dumpState(const QString& tag,
            const RenderingSystem& rs,
            QOpenGLWidget* vp,
            QOpenGLFunctions_4_3_Core* gl);

    private:
        BlackBox();                        // ctor is private –- singleton
        ~BlackBox() = default;

        QFile m_file;
    };

} // namespace dbg
