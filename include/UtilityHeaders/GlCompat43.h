#pragma once
// GL 4.2/4.3 enum constants missing from Apple's OpenGL headers.
//
// macOS caps its OpenGL headers (<OpenGL/gl3.h>) at GL 4.1. The renderer's
// FUNCTIONS come from Qt's QOpenGLFunctions_4_3_Core wrappers (declared on
// every desktop platform), but the enum MACROS come from the system headers —
// so exactly these constants are undeclared on macOS (ci-mac run 138, the
// first AppleClang compile of the app; full token census in that session).
//
// The values are fixed by the Khronos OpenGL registry, identical on every
// platform. This header is force-included on APPLE builds only (see the
// target_compile_options block in CMakeLists.txt); other platforms get them
// from their own GL headers, and every define is #ifndef-guarded so a future
// SDK providing them wins.
//
// Honest boundary: this makes the 4.3 code paths COMPILE on macOS. Running
// them there still requires the KRS Graphics Vulkan/Metal port — macOS never
// shipped GL 4.3 (docs/graphics/IMPLEMENTATION_PLAN.md).

// --- GL 4.2 (ARB_shader_image_load_store barrier bits) ---
#ifndef GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT
#define GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT 0x00000001
#endif
#ifndef GL_TEXTURE_FETCH_BARRIER_BIT
#define GL_TEXTURE_FETCH_BARRIER_BIT 0x00000008
#endif
#ifndef GL_SHADER_IMAGE_ACCESS_BARRIER_BIT
#define GL_SHADER_IMAGE_ACCESS_BARRIER_BIT 0x00000020
#endif
#ifndef GL_COMMAND_BARRIER_BIT
#define GL_COMMAND_BARRIER_BIT 0x00000040
#endif
#ifndef GL_BUFFER_UPDATE_BARRIER_BIT
#define GL_BUFFER_UPDATE_BARRIER_BIT 0x00000200
#endif

// --- GL 4.3 (compute shaders + shader storage buffers) ---
#ifndef GL_COMPUTE_SHADER
#define GL_COMPUTE_SHADER 0x91B9
#endif
#ifndef GL_SHADER_STORAGE_BUFFER
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#endif
#ifndef GL_SHADER_STORAGE_BARRIER_BIT
#define GL_SHADER_STORAGE_BARRIER_BIT 0x00002000
#endif

// --- Legacy sRGB alias some Apple SDKs omit from the core-profile header ---
#ifndef GL_SRGB_ALPHA
#define GL_SRGB_ALPHA 0x8C42
#endif
