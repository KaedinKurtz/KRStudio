#pragma once
// KRS Graphics version contract. Compile-time macros for consumers; GetVersion()
// reports what the linked binary was built as, so a consumer can assert
// header/binary agreement at startup (debug builds of krsg-qt do this for you).

#include <cstdint>

#define KRSG_VERSION_MAJOR 0
#define KRSG_VERSION_MINOR 2
#define KRSG_VERSION_PATCH 0

// Placeholder until shared builds land (WP1 adds real export handling).
#ifndef KRSG_API
#define KRSG_API
#endif

namespace krsg
{

struct Version {
    std::uint32_t major;
    std::uint32_t minor;
    std::uint32_t patch;
};

KRSG_API Version GetVersion();

} // namespace krsg
