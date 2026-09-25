// Vulkan backend: resources. Facade validated everything already; this file
// only creates/destroys GPU objects and moves bytes. Host-visible and readback
// memory is persistently mapped by VMA; flush/invalidate is done on every
// write/read (correct on non-coherent memory, a no-op cost on coherent).

#include <cstring>

#include "rhi/vulkan/vk_common.h"

#include <vk_mem_alloc.h>

namespace krsg::vulkan
{

ResultCode CreateBuffer(core::DeviceImpl* impl, core::BufferRecord& record)
{
    core::VulkanState* state = impl->vk;

    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = record.desc.size;
    bufferInfo.usage = ToVkBufferUsage(record.desc.usage);
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;
    if (record.desc.memory == MemoryLocation::HostVisible) {
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    } else if (record.desc.memory == MemoryLocation::Readback) {
        allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }

    auto* obj = new BufferObj();
    VmaAllocationInfo outInfo{};
    const VkResult vr =
        vmaCreateBuffer(state->allocator, &bufferInfo, &allocInfo, &obj->buffer, &obj->allocation, &outInfo);
    if (vr != VK_SUCCESS) {
        delete obj;
        return FailVk("vmaCreateBuffer", vr);
    }
    obj->mapped = outInfo.pMappedData;
    record.backend = reinterpret_cast<std::uint64_t>(obj);
    return ResultCode::Ok;
}

void DestroyBuffer(core::DeviceImpl* impl, core::BufferRecord& record)
{
    auto* obj = reinterpret_cast<BufferObj*>(record.backend);
    if (obj == nullptr) {
        return;
    }
    // v0 execution is synchronous (submitCompute waits), so no deferred-release
    // queue is needed yet; it arrives with frame pacing in the WP1 continuation.
    vmaDestroyBuffer(impl->vk->allocator, obj->buffer, obj->allocation);
    delete obj;
    record.backend = 0;
}

ResultCode WriteBuffer(core::DeviceImpl* impl, core::BufferRecord& record, std::uint64_t offset, const void* data,
                       std::uint64_t size)
{
    auto* obj = reinterpret_cast<BufferObj*>(record.backend);
    if (obj == nullptr || obj->mapped == nullptr) {
        return ResultCode::Internal;
    }
    std::memcpy(static_cast<std::uint8_t*>(obj->mapped) + offset, data, static_cast<std::size_t>(size));
    vmaFlushAllocation(impl->vk->allocator, obj->allocation, offset, size);
    return ResultCode::Ok;
}

ResultCode ReadBuffer(core::DeviceImpl* impl, core::BufferRecord& record, std::uint64_t offset, void* out,
                      std::uint64_t size)
{
    auto* obj = reinterpret_cast<BufferObj*>(record.backend);
    if (obj == nullptr || obj->mapped == nullptr) {
        return ResultCode::Internal;
    }
    vmaInvalidateAllocation(impl->vk->allocator, obj->allocation, offset, size);
    std::memcpy(out, static_cast<const std::uint8_t*>(obj->mapped) + offset, static_cast<std::size_t>(size));
    return ResultCode::Ok;
}

ResultCode CreateImage(core::DeviceImpl* impl, core::ImageRecord& record)
{
    core::VulkanState* state = impl->vk;
    const ImageDesc& desc = record.desc;

    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = desc.type == ImageType::Tex3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    imageInfo.format = ToVkFormat(desc.format);
    imageInfo.extent = {desc.width, desc.height, desc.depth};
    imageInfo.mipLevels = desc.mipLevels;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = ToVkImageUsage(desc.usage);
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    VmaAllocationCreateInfo allocInfo{};
    allocInfo.usage = VMA_MEMORY_USAGE_AUTO;

    auto* obj = new ImageObj();
    VkResult vr = vmaCreateImage(state->allocator, &imageInfo, &allocInfo, &obj->image, &obj->allocation, nullptr);
    if (vr != VK_SUCCESS) {
        delete obj;
        return FailVk("vmaCreateImage", vr);
    }

    const bool isDepth = desc.format == Format::D32Float;
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = obj->image;
    viewInfo.viewType = desc.type == ImageType::Tex3D ? VK_IMAGE_VIEW_TYPE_3D : VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = imageInfo.format;
    viewInfo.subresourceRange.aspectMask = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = desc.mipLevels;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;
    vr = vkCreateImageView(state->device, &viewInfo, nullptr, &obj->view);
    if (vr != VK_SUCCESS) {
        vmaDestroyImage(state->allocator, obj->image, obj->allocation);
        delete obj;
        return FailVk("vkCreateImageView", vr);
    }

    record.backend = reinterpret_cast<std::uint64_t>(obj);
    return ResultCode::Ok;
}

void DestroyImage(core::DeviceImpl* impl, core::ImageRecord& record)
{
    auto* obj = reinterpret_cast<ImageObj*>(record.backend);
    if (obj == nullptr) {
        return;
    }
    vkDestroyImageView(impl->vk->device, obj->view, nullptr);
    vmaDestroyImage(impl->vk->allocator, obj->image, obj->allocation);
    delete obj;
    record.backend = 0;
}

ResultCode CreateSampler(core::DeviceImpl* impl, core::SamplerRecord& record)
{
    const SamplerDesc& desc = record.desc;
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = ToVkFilter(desc.magFilter);
    samplerInfo.minFilter = ToVkFilter(desc.minFilter);
    samplerInfo.mipmapMode =
        desc.mipFilter == Filter::Nearest ? VK_SAMPLER_MIPMAP_MODE_NEAREST : VK_SAMPLER_MIPMAP_MODE_LINEAR;
    samplerInfo.addressModeU = ToVkAddressMode(desc.addressU);
    samplerInfo.addressModeV = ToVkAddressMode(desc.addressV);
    samplerInfo.addressModeW = ToVkAddressMode(desc.addressW);
    samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
    samplerInfo.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;

    VkSampler sampler = VK_NULL_HANDLE;
    const VkResult vr = vkCreateSampler(impl->vk->device, &samplerInfo, nullptr, &sampler);
    if (vr != VK_SUCCESS) {
        return FailVk("vkCreateSampler", vr);
    }
    record.backend = reinterpret_cast<std::uint64_t>(sampler);
    return ResultCode::Ok;
}

void DestroySampler(core::DeviceImpl* impl, core::SamplerRecord& record)
{
    if (record.backend != 0) {
        vkDestroySampler(impl->vk->device, reinterpret_cast<VkSampler>(record.backend), nullptr);
        record.backend = 0;
    }
}

ResultCode CreateShaderModule(core::DeviceImpl* impl, core::ShaderRecord& record, const ShaderModuleDesc& desc)
{
    VkShaderModuleCreateInfo moduleInfo{};
    moduleInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    moduleInfo.codeSize = desc.wordCount * sizeof(std::uint32_t);
    moduleInfo.pCode = desc.spirv;

    VkShaderModule module = VK_NULL_HANDLE;
    const VkResult vr = vkCreateShaderModule(impl->vk->device, &moduleInfo, nullptr, &module);
    if (vr != VK_SUCCESS) {
        return FailVk("vkCreateShaderModule", vr);
    }
    record.backend = reinterpret_cast<std::uint64_t>(module);
    return ResultCode::Ok;
}

void DestroyShaderModule(core::DeviceImpl* impl, core::ShaderRecord& record)
{
    if (record.backend != 0) {
        vkDestroyShaderModule(impl->vk->device, reinterpret_cast<VkShaderModule>(record.backend), nullptr);
        record.backend = 0;
    }
}

} // namespace krsg::vulkan
