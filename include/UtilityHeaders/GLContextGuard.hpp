#pragma once

#include <QOpenGLWidget>

/**
 * @class GLContextGuard
 * @brief A robust RAII wrapper to manage an OpenGL context.
 *
 * This class ensures that an QOpenGLWidget's context is made current
 * upon construction and is guaranteed to be released upon destruction,
 * even if exceptions are thrown or the scope is exited early. This prevents
 * context-related crashes and is a best practice for Qt OpenGL programming.
 */
class GLContextGuard
{
public:
    // Constructor: Makes the widget's OpenGL context current.
    explicit GLContextGuard(QOpenGLWidget* widget) : m_widget(widget)
    {
        if (m_widget) {
            m_widget->makeCurrent();
        }
    }

    // Destructor: Releases the OpenGL context.
    ~GLContextGuard()
    {
        if (m_widget) {
            m_widget->doneCurrent();
        }
    }

    // --- Deleted functions to prevent copying ---
    // This ensures the guard cannot be accidentally copied, which would
    // lead to incorrect behavior (releasing the context too early).
    GLContextGuard(const GLContextGuard&) = delete;
    GLContextGuard& operator=(const GLContextGuard&) = delete;
    GLContextGuard(GLContextGuard&&) = delete;
    GLContextGuard& operator=(GLContextGuard&&) = delete;

private:
    QOpenGLWidget* m_widget; // A raw pointer is fine, the guard's lifetime is very short.
};
