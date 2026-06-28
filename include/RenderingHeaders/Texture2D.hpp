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

    // Load from compressed image bytes in memory (embedded glb/fbx textures).
    bool loadFromMemory(const unsigned char* data, size_t size, bool gammaCorrection = false);

    // Load a floating-point HDR (.hdr) as a GL_RGB16F texture via stbi_loadf.
    // Unlike loadFromFile (which decodes to clamped 8-bit), this preserves the
    // full dynamic range needed for correct image-based lighting.
    bool loadHDR(const std::string& path);

    // Analyse an equirectangular HDR (CPU-side) to derive a directional "sun" from the
    // environment: finds the brightest texel (the sun) -> its world-space TRAVEL direction
    // (out[3], like u_sunDir) and the normalized colour/temperature of a region around it
    // (out[3], max channel = 1). Matches loadHDR's vertical flip + the equirect mapping in
    // equirect_to_cubemap_frag.glsl. Returns false if the file can't be read.
    static bool analyzeHdrSun(const std::string& path, float outSunDir[3], float outSunColor[3]);

    // Generate an empty texture of given size and formats, optionally uploading data.
    void generate(int width, int height,
        GLenum internalFormat = GL_RGBA8,
        GLenum dataFormat = GL_RGBA,
        const void* data = nullptr);

    // Generate a FLOAT texture (e.g. GL_RGBA32F lookup tables) from raw float data,
    // uploading with GL_FLOAT. Unlike generate() (which hardcodes GL_UNSIGNED_BYTE and
    // would clamp values to [0,1]), this preserves full float range. CLAMP_TO_EDGE +
    // LINEAR, no mipmaps — the layout LTC/BRDF LUTs need.
    void generateFloat(int width, int height,
        GLenum internalFormat, GLenum dataFormat, const float* data);

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
