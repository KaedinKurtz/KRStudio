// VulkanMemoryAllocator implementation TU — the single place VMA_IMPLEMENTATION
// is defined. VMA resolves entry points dynamically through the volk-provided
// vkGetInstanceProcAddr/vkGetDeviceProcAddr (see CreateVulkanDevice), so this
// TU carries no static Vulkan link dependency either.

#include "rhi/vulkan/vk_common.h"

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#endif

// VMA_STATIC_VULKAN_FUNCTIONS=0 / VMA_DYNAMIC_VULKAN_FUNCTIONS=1 are target-level
// compile definitions (CMake) so every TU that includes the header agrees.
#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
