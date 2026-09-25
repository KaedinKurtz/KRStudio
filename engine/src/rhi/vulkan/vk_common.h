#pragma once
// Shared internals of the Vulkan backend. The ONLY headers that may include
// Vulkan things live under src/rhi/vulkan (S0-enforced). volk loads the loader
// at runtime via dlopen/LoadLibrary, so the engine links no Vulkan SDK — on a
// machine without a driver, device creation reports Unsupported instead of
// failing to load the binary (ADR-003).

#include <volk.h>

#include "core/device_impl.h"

// Global-scope forward declarations of VMA's opaque types (vk_mem_alloc.h
// defines them; declaring them inside our namespaces would silently mint
// unrelated types).
struct VmaAllocator_T;
struct VmaAllocation_T;

namespace krsg::core
{

// The backend half of DeviceImpl. Owned by CreateVulkanDevice/DestroyVulkanDevice.
struct VulkanState {
    VkInstance instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    std::uint32_t queueFamily = 0;
    VkQueue queue = VK_NULL_HANDLE;

    VmaAllocator_T* allocator = nullptr; // ::VmaAllocator without pulling vk_mem_alloc.h here

    // v0 synchronous execution objects (one in flight by contract).
    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    VkDescriptorPool descriptorPool = VK_NULL_HANDLE;

    bool nonCoherentMemory = true; // conservatively flush/invalidate unless proven coherent
};

} // namespace krsg::core

namespace krsg::vulkan
{

// Heap-side objects referenced by the facade records' opaque `backend` cookie.
struct BufferObj {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation_T* allocation = nullptr;
    void* mapped = nullptr; // non-null for HostVisible/Readback (persistently mapped)
};

struct ImageObj {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation_T* allocation = nullptr;
    VkImageView view = VK_NULL_HANDLE;
};

struct PipelineObj {
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    std::uint32_t pushConstantSize = 0;
    std::uint32_t storageBufferBindings = 0;
};

// Report a VkResult through the debug sink and map it to a ResultCode.
krsg::ResultCode FailVk(const char* what, VkResult result);

inline VkFormat ToVkFormat(krsg::Format format)
{
    switch (format) {
    case krsg::Format::Unknown:
        return VK_FORMAT_UNDEFINED;
    case krsg::Format::R8Unorm:
        return VK_FORMAT_R8_UNORM;
    case krsg::Format::RG8Unorm:
        return VK_FORMAT_R8G8_UNORM;
    case krsg::Format::RGBA8Unorm:
        return VK_FORMAT_R8G8B8A8_UNORM;
    case krsg::Format::RGBA8Srgb:
        return VK_FORMAT_R8G8B8A8_SRGB;
    case krsg::Format::R16Float:
        return VK_FORMAT_R16_SFLOAT;
    case krsg::Format::RG16Float:
        return VK_FORMAT_R16G16_SFLOAT;
    case krsg::Format::RGBA16Float:
        return VK_FORMAT_R16G16B16A16_SFLOAT;
    case krsg::Format::R32Float:
        return VK_FORMAT_R32_SFLOAT;
    case krsg::Format::RG32Float:
        return VK_FORMAT_R32G32_SFLOAT;
    case krsg::Format::RGBA32Float:
        return VK_FORMAT_R32G32B32A32_SFLOAT;
    case krsg::Format::R32Uint:
        return VK_FORMAT_R32_UINT;
    case krsg::Format::RG32Uint:
        return VK_FORMAT_R32G32_UINT;
    case krsg::Format::RGBA32Uint:
        return VK_FORMAT_R32G32B32A32_UINT;
    case krsg::Format::D32Float:
        return VK_FORMAT_D32_SFLOAT;
    }
    return VK_FORMAT_UNDEFINED;
}

inline VkBufferUsageFlags ToVkBufferUsage(krsg::BufferUsage usage)
{
    VkBufferUsageFlags flags = 0;
    if (krsg::HasUsage(usage, krsg::BufferUsage::Vertex)) {
        flags |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    }
    if (krsg::HasUsage(usage, krsg::BufferUsage::Index)) {
        flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    }
    if (krsg::HasUsage(usage, krsg::BufferUsage::Uniform)) {
        flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    }
    if (krsg::HasUsage(usage, krsg::BufferUsage::Storage)) {
        flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
    }
    if (krsg::HasUsage(usage, krsg::BufferUsage::Indirect)) {
        flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    }
    if (krsg::HasUsage(usage, krsg::BufferUsage::TransferSrc)) {
        flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    }
    if (krsg::HasUsage(usage, krsg::BufferUsage::TransferDst)) {
        flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    }
    return flags;
}

inline VkImageUsageFlags ToVkImageUsage(krsg::ImageUsage usage)
{
    VkImageUsageFlags flags = 0;
    if (krsg::HasUsage(usage, krsg::ImageUsage::Sampled)) {
        flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
    }
    if (krsg::HasUsage(usage, krsg::ImageUsage::Storage)) {
        flags |= VK_IMAGE_USAGE_STORAGE_BIT;
    }
    if (krsg::HasUsage(usage, krsg::ImageUsage::ColorAttachment)) {
        flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if (krsg::HasUsage(usage, krsg::ImageUsage::DepthAttachment)) {
        flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    }
    if (krsg::HasUsage(usage, krsg::ImageUsage::TransferSrc)) {
        flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    if (krsg::HasUsage(usage, krsg::ImageUsage::TransferDst)) {
        flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    }
    return flags;
}

inline VkFilter ToVkFilter(krsg::Filter filter)
{
    return filter == krsg::Filter::Nearest ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
}

inline VkSamplerAddressMode ToVkAddressMode(krsg::AddressMode mode)
{
    switch (mode) {
    case krsg::AddressMode::Repeat:
        return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case krsg::AddressMode::ClampToEdge:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    case krsg::AddressMode::ClampToBorder:
        return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    }
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
}

} // namespace krsg::vulkan
