#pragma once
// Engine-internal device state. The public krsg::Device is a facade over this;
// backends fill in their half at creation (null always; Vulkan when built in).

#include <cstdint>

#include <krsg/types.h>

#include "core/handle_table.h"

namespace krsg::core
{

struct BufferRecord {
    BufferDesc desc{};
};

struct VulkanState; // defined by src/rhi/vulkan when KRSG_HAS_VULKAN

struct DeviceImpl : Device {
    DeviceCaps capsData{};
    HandleTable<BufferRecord> buffers;
    VulkanState* vk = nullptr;

    DeviceImpl() = default;
    ~DeviceImpl() = default;
};

void EmitDebug(Severity severity, const char* message);

} // namespace krsg::core

#if defined(KRSG_HAS_VULKAN)
namespace krsg::vulkan
{
// Fills impl->capsData and impl->vk; returns Ok on success.
krsg::ResultCode CreateVulkanDevice(const krsg::DeviceDesc& desc, krsg::core::DeviceImpl* impl);
void DestroyVulkanDevice(krsg::core::DeviceImpl* impl);
} // namespace krsg::vulkan
#endif
