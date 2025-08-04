#pragma once

#include <memory>
#include <string>
#include <vector>

// Include the full OpenGL functions header to define GLuint and other types.
#include <QOpenGLFunctions_4_3_Core>

// Forward declaration to avoid a circular include with Texture2D.hpp
class Texture2D;

struct Cubemap {
public:
    // Constructor and Destructor
    Cubemap();
    ~Cubemap();

    /// Render an equirectangular Texture2D into a cubemap.
    static std::shared_ptr<Cubemap> fromEquirectangular(
        const Texture2D& hdrEquirect,
        QOpenGLFunctions_4_3_Core* gl,
        const std::string& vertPath,
        const std::string& fragPath,
        int faceSize);

    /// Convolve a source cubemap into a low-frequency irradiance map.
    static std::shared_ptr<Cubemap> convolveIrradiance(
        const Cubemap& source,
        QOpenGLFunctions_4_3_Core* gl,
        const std::string& vertPath,
        const std::string& fragPath,
        int faceSize);

    /// Prefilter an environment map into a specular map with mipmaps.
    static std::shared_ptr<Cubemap> prefilter(
        const Cubemap& source,
        QOpenGLFunctions_4_3_Core* gl,
        const std::string& vertPath,
        const std::string& fragPath,
        int baseSize,
        int maxMipLevels);

    /// Bind this cubemap to the specified texture unit.
    void bind(QOpenGLFunctions_4_3_Core* gl, unsigned int unit) const;

    /// Retrieve the raw OpenGL texture ID.
    GLuint getID() const;

private:
    GLuint _id = 0;
    int    _size = 0;
    int    _levels = 0;

    // Prevent copying to avoid issues with GPU resource ownership.
    Cubemap(const Cubemap&) = delete;
    Cubemap& operator=(const Cubemap&) = delete;
};
