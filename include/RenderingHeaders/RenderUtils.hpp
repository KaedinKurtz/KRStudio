#pragma once
#include <vector>
#include "IRenderPass.hpp"   // pulls in RenderFrameContext

namespace RenderUtils {

    /// Bind an FBO, set viewport & draw buffers, disable scissor, then clear.
    /// Always call this as the VERY FIRST thing in your execute().
    inline void prepareFBO(QOpenGLFunctions_4_3_Core* gl,
        GLuint fbo,
        int width,
        int height,
        const std::vector<GLenum>& colorAttachments,
        bool clearColor,
        bool clearDepth,
        bool clearStencil)
    {
        // 1) bind
        gl->glBindFramebuffer(GL_FRAMEBUFFER, fbo);

        // 2) full?target viewport
        gl->glViewport(0, 0, width, height);

        // 3) no scissor
        gl->glDisable(GL_SCISSOR_TEST);

        // 4) set draw buffers
        if (!colorAttachments.empty()) {
            gl->glDrawBuffers(GLsizei(colorAttachments.size()), colorAttachments.data());
        }
        else {
            gl->glDrawBuffer(GL_NONE);
        }

        // 5) build clear mask
        GLbitfield mask = 0;
        if (clearColor)   mask |= GL_COLOR_BUFFER_BIT;
        if (clearDepth)   mask |= GL_DEPTH_BUFFER_BIT;
        if (clearStencil) mask |= GL_STENCIL_BUFFER_BIT;

        // 6) clear
        gl->glClear(mask);
    }

} // namespace RenderUtils
