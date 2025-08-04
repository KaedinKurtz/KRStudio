#pragma once

#include <memory>
#include <string>
#include <glm/vec4.hpp> // For glm::u8vec4
#include <QOpenGLFunctions_4_3_Core>
#include <QOpenGLTexture> // For QOpenGLTexture, if needed

// Forward-declare the classes we need pointers/references to
class QOpenGLFunctions_4_3_Core;
struct Texture2D;

namespace GLUtils {

    // This is now a free function inside the GLUtils namespace
    std::shared_ptr<Texture2D> generateBRDFLUT(
        QOpenGLFunctions_4_3_Core* gl,
        const std::string& vertPath,
        const std::string& fragPath,
        int size);

} // namespace GLUtils