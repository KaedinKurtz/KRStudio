#pragma once
// KRS Graphics — umbrella header.
//
// The standalone rendering library split out of KRStudio (docs/graphics/ARCHITECTURE.md).
// Public API rules: C++17, no Vulkan/Qt/GL/GLM types, opaque handles, POD descriptors,
// Result-based errors. Consumers include <krsg/krsg.h> (or individual headers) and link
// the installed KRSGraphics package: find_package(KRSGraphics CONFIG REQUIRED),
// target_link_libraries(app PRIVATE krsg::krsg).

#include <krsg/types.h>
#include <krsg/version.h>
