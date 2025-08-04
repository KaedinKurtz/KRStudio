#pragma once

#include <string>
#include <memory>
#include <vector>
#include <QOpenGLFunctions_4_3_Core>
#include <glm/glm.hpp>
#include <stb_image.h>

// A utility class for managing 2D OpenGL textures using Qt's QOpenGLFunctions_4_3_Core.
// Supports loading from file, manual generation, wrapping/filtering settings,
// mipmap generation, downscaling, and tiling.

class Texture2D : protected QOpenGLFunctions_4_3_Core {
public:
    // Constructors & Destructor
    Texture2D();
    ~Texture2D();

    // Load from image file. Uses stb_image under the hood.
    // If gammaCorrection is true, assumes the image is in sRGB space and converts.
    bool loadFromFile(const std::string& path, bool gammaCorrection = false);

    // Generate an empty texture of given size and formats, optionally uploading data.
    void generate(int width, int height,
        GLenum internalFormat = GL_RGBA8,
        GLenum dataFormat = GL_RGBA,
        const void* data = nullptr);

    // Bind to texture unit slot (0..GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS-1)
    void bind(unsigned int unit = 0) const;

    // Set wrapping parameters (default: GL_REPEAT)
    void setWrap(GLenum wrapS, GLenum wrapT);

    // Set filtering parameters (default: GL_LINEAR for mag, GL_LINEAR_MIPMAP_LINEAR for min)
    void setFilter(GLenum minFilter, GLenum magFilter);

    // Generate mipmaps. Must have a complete texture.
    void generateMipmaps();

    // Downscale the texture by integer factors (reloads to GPU).
    // Returns true on success.
    bool downscale(int factorX, int factorY);

    // Create a new tiled texture by repeating this texture tileX x tileY times.
    // Returns a new Texture2D object.
    std::shared_ptr<Texture2D> tiled(int tileX, int tileY) const;

    // Getters
    inline GLuint getID()          const { return _id; }
    inline int    getWidth()       const { return _width; }
    inline int    getHeight()      const { return _height; }
    inline GLenum getInternalFmt() const { return _internalFormat; }
    inline GLenum getDataFmt()     const { return _dataFormat; }
   
    static std::shared_ptr<Texture2D> createFromColor(QOpenGLFunctions_4_3_Core* gl, glm::u8vec4 color);

    GLuint _id = 0;
        int    _width = 0;
        int    _height = 0;
        GLenum _internalFormat = GL_RGBA8;
        GLenum _dataFormat = GL_RGBA;
private:
   

    // Helper: load raw pixels via stb_image
    unsigned char* _loadPixels(const std::string& path, int& outW, int& outH, int& outChannels);

    // Prevent copying
    Texture2D(const Texture2D&) = delete;
    Texture2D& operator=(const Texture2D&) = delete;
};
