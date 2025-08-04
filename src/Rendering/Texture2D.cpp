#include "Texture2D.hpp"
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
#include "Shader.hpp"
#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLFunctions_4_3_Core>
#include <QOpenGLVersionFunctionsFactory>
#include <vector>

// Helper to get a fullscreen quad VAO, creating it if necessary.
static GLuint getFullscreenQuadVAO(QOpenGLFunctions_4_3_Core* gl) {
    static GLuint quadVAO = 0;
    if (quadVAO == 0) {
        float quadVertices[] = {
            // positions        // texture Coords
            -1.0f,  1.0f, 0.0f, 0.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
             1.0f,  1.0f, 0.0f, 1.0f, 1.0f,
             1.0f, -1.0f, 0.0f, 1.0f, 0.0f,
        };
        // setup plane VAO
        GLuint quadVBO;
        gl->glGenVertexArrays(1, &quadVAO);
        gl->glGenBuffers(1, &quadVBO);
        gl->glBindVertexArray(quadVAO);
        gl->glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
        gl->glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices, GL_STATIC_DRAW);
        gl->glEnableVertexAttribArray(0);
        gl->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        gl->glEnableVertexAttribArray(1);
        gl->glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
    }
    return quadVAO;
}


Texture2D::Texture2D() {
    // Initialize OpenGL function pointers
    initializeOpenGLFunctions();
}

Texture2D::~Texture2D() {
    if (_id) {
        glDeleteTextures(1, &_id);
        _id = 0;
    }
}

bool Texture2D::loadFromFile(const std::string& path, bool gammaCorrection) {
    int w, h, channels;
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path.c_str(), &w, &h, &channels, 0);
    if (!data) {
        qWarning() << "Texture2D: Failed to load image:" << QString::fromStdString(path);
        return false;
    }

    GLenum internalFormat = 0, dataFormat = 0;
    if (channels == 1) {
        internalFormat = GL_RED;
        dataFormat = GL_RED;
    }
    else if (channels == 3) {
        internalFormat = gammaCorrection ? GL_SRGB : GL_RGB;
        dataFormat = GL_RGB;
    }
    else if (channels == 4) {
        internalFormat = gammaCorrection ? GL_SRGB_ALPHA : GL_RGBA;
        dataFormat = GL_RGBA;
    }

    generate(w, h, internalFormat, dataFormat, data);

    stbi_image_free(data);
    return true;
}

void Texture2D::generate(int width, int height, GLenum internalFormat, GLenum dataFormat, const void* data) {
    _width = width;
    _height = height;
    _internalFormat = internalFormat;
    _dataFormat = dataFormat;

    if (!_id) glGenTextures(1, &_id);
    glBindTexture(GL_TEXTURE_2D, _id);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, dataFormat, GL_UNSIGNED_BYTE, data);

    // Default parameters
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture2D::bind(unsigned int unit) const {
    auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(QOpenGLContext::currentContext());
    if (gl) {
        gl->glActiveTexture(GL_TEXTURE0 + unit);
        gl->glBindTexture(GL_TEXTURE_2D, _id);
    }
}

void Texture2D::setWrap(GLenum wrapS, GLenum wrapT) {
    glBindTexture(GL_TEXTURE_2D, _id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture2D::setFilter(GLenum minFilter, GLenum magFilter) {
    glBindTexture(GL_TEXTURE_2D, _id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, magFilter);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Texture2D::generateMipmaps() {
    glBindTexture(GL_TEXTURE_2D, _id);
    glGenerateMipmap(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, 0);
}

bool Texture2D::downscale(int factorX, int factorY) {
    if (factorX <= 1 && factorY <= 1) return false;
    int newW = _width / factorX;
    int newH = _height / factorY;
    if (newW < 1 || newH < 1) return false;

    int channels = (_dataFormat == GL_RGBA || _dataFormat == GL_SRGB_ALPHA) ? 4 : 3;
    std::vector<unsigned char> pixels(_width * _height * channels);
    glBindTexture(GL_TEXTURE_2D, _id);
    glGetTexImage(GL_TEXTURE_2D, 0, _dataFormat, GL_UNSIGNED_BYTE, pixels.data());

    std::vector<unsigned char> scaled(newW * newH * channels);

    // Simple box filter
    for (int y = 0; y < newH; ++y) {
        for (int x = 0; x < newW; ++x) {
            int dstIdx = (y * newW + x) * channels;
            for (int c = 0; c < channels; ++c) {
                unsigned int sum = 0;
                for (int ky = 0; ky < factorY; ++ky) {
                    for (int kx = 0; kx < factorX; ++kx) {
                        int srcX = x * factorX + kx;
                        int srcY = y * factorY + ky;
                        int srcIdx = (srcY * _width + srcX) * channels + c;
                        sum += pixels[srcIdx];
                    }
                }
                scaled[dstIdx + c] = static_cast<unsigned char>(sum / (factorX * factorY));
            }
        }
    }

    generate(newW, newH, _internalFormat, _dataFormat, scaled.data());
    return true;
}

std::shared_ptr<Texture2D> Texture2D::tiled(int tileX, int tileY) const {
    int newW = _width * tileX;
    int newH = _height * tileY;
    int channels = (_dataFormat == GL_RGBA || _dataFormat == GL_SRGB_ALPHA) ? 4 : 3;
    std::vector<unsigned char> pixels(_width * _height * channels);

    auto* gl = QOpenGLVersionFunctionsFactory::get<QOpenGLFunctions_4_3_Core>(QOpenGLContext::currentContext());

    gl->glBindTexture(GL_TEXTURE_2D, _id);
    gl->glGetTexImage(GL_TEXTURE_2D, 0, _dataFormat, GL_UNSIGNED_BYTE, pixels.data());

    std::vector<unsigned char> tiledPixels(newW * newH * channels);

    for (int ty = 0; ty < tileY; ++ty) {
        for (int tx = 0; tx < tileX; ++tx) {
            for (int y = 0; y < _height; ++y) {
                for (int x = 0; x < _width; ++x) {
                    int srcIdx = (y * _width + x) * channels;
                    int dstIdx = ((ty * _height + y) * newW + (tx * _width + x)) * channels;
                    for (int c = 0; c < channels; ++c) {
                        tiledPixels[dstIdx + c] = pixels[srcIdx + c];
                    }
                }
            }
        }
    }

    auto newTex = std::make_shared<Texture2D>();
    newTex->generate(newW, newH, _internalFormat, _dataFormat, tiledPixels.data());
    return newTex;
}

unsigned char* Texture2D::_loadPixels(const std::string& path, int& outW, int& outH, int& outChannels) {
    stbi_set_flip_vertically_on_load(true);
    unsigned char* data = stbi_load(path.c_str(), &outW, &outH, &outChannels, 0);
    return data;
}

std::shared_ptr<Texture2D> Texture2D::createFromColor(QOpenGLFunctions_4_3_Core* gl, glm::u8vec4 color)
{
    // Create a new Texture2D object, managed by a shared_ptr.
    auto tex = std::make_shared<Texture2D>();

    // Use the existing public 'generate' method to create the 1x1 texture.
    // This correctly handles all internal state and OpenGL calls, avoiding access errors.
    tex->generate(
        1, 1,           // width and height
        GL_RGBA8,       // internal format
        GL_RGBA,        // data format
        &color[0]       // pointer to the color data
    );

    // The 'generate' function already sets up the texture, so we just return it.
    return tex;
}