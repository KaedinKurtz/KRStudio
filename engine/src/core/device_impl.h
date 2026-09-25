#pragma once
// Engine-internal device state. The public krsg::Device is a facade over this;
// backends fill in their half at creation (null always; Vulkan when built in).
// Facade owns: descriptor validation, handle tables, null-backend semantics.
// Backend owns: GPU objects, memory, execution. Backend-object lifetime is
// keyed to the record's opaque `backend` cookie (an index/pointer the backend
// interprets; the facade never does).

#include <cstdint>
#include <vector>

#include <krsg/device.h>
#include <krsg/types.h>

#include "core/handle_table.h"

namespace krsg::core
{

struct BufferRecord {
    BufferDesc desc{};
    // Null backend: HostVisible/Readback buffers hold real bytes so unit tests
    // exercise true write/read semantics. Vulkan leaves this empty and maps
    // through VMA instead.
    std::vector<std::uint8_t> hostData;
    std::uint64_t backend = 0;
};

struct ImageRecord {
    ImageDesc desc{};
    std::uint64_t backend = 0;
};

struct SamplerRecord {
    SamplerDesc desc{};
    std::uint64_t backend = 0;
};

struct ShaderRecord {
    std::uint32_t wordCount = 0;
    std::uint64_t backend = 0;
};

struct PipelineRecord {
    ComputePipelineDesc desc{}; // shader handle + layout shape (entryPoint not retained)
    std::uint64_t backend = 0;
};

struct VulkanState; // defined by src/rhi/vulkan when KRSG_HAS_VULKAN

struct DeviceImpl : Device {
    DeviceCaps capsData{};
    HandleTable<BufferRecord> buffers;
    HandleTable<ImageRecord> images;
    HandleTable<SamplerRecord> samplers;
    HandleTable<ShaderRecord> shaders;
    HandleTable<PipelineRecord> pipelines;
    VulkanState* vk = nullptr;

    DeviceImpl() = default;
    ~DeviceImpl() = default;
};

void EmitDebug(Severity severity, const char* message);

// Bytes per texel for the public Format enum (0 for Unknown). Shared by
// facade validation and both backends.
std::uint32_t FormatTexelSize(Format format);

} // namespace krsg::core

#if defined(KRSG_HAS_VULKAN)
// The Vulkan half of the facade. Every function validates nothing (the facade
// already did) and reports failures through ResultCode + the debug sink.
namespace krsg::vulkan
{
krsg::ResultCode CreateVulkanDevice(const krsg::DeviceDesc& desc, krsg::core::DeviceImpl* impl);
void DestroyVulkanDevice(krsg::core::DeviceImpl* impl);

krsg::ResultCode CreateBuffer(krsg::core::DeviceImpl* impl, krsg::core::BufferRecord& record);
void DestroyBuffer(krsg::core::DeviceImpl* impl, krsg::core::BufferRecord& record);
krsg::ResultCode WriteBuffer(krsg::core::DeviceImpl* impl, krsg::core::BufferRecord& record, std::uint64_t offset,
                             const void* data, std::uint64_t size);
krsg::ResultCode ReadBuffer(krsg::core::DeviceImpl* impl, krsg::core::BufferRecord& record, std::uint64_t offset,
                            void* out, std::uint64_t size);

krsg::ResultCode CreateImage(krsg::core::DeviceImpl* impl, krsg::core::ImageRecord& record);
void DestroyImage(krsg::core::DeviceImpl* impl, krsg::core::ImageRecord& record);
krsg::ResultCode CreateSampler(krsg::core::DeviceImpl* impl, krsg::core::SamplerRecord& record);
void DestroySampler(krsg::core::DeviceImpl* impl, krsg::core::SamplerRecord& record);

krsg::ResultCode CreateShaderModule(krsg::core::DeviceImpl* impl, krsg::core::ShaderRecord& record,
                                    const krsg::ShaderModuleDesc& desc);
void DestroyShaderModule(krsg::core::DeviceImpl* impl, krsg::core::ShaderRecord& record);
krsg::ResultCode CreateComputePipeline(krsg::core::DeviceImpl* impl, krsg::core::PipelineRecord& record,
                                       const krsg::ComputePipelineDesc& desc, std::uint64_t shaderBackend);
void DestroyPipeline(krsg::core::DeviceImpl* impl, krsg::core::PipelineRecord& record);

krsg::ResultCode SubmitCompute(krsg::core::DeviceImpl* impl, const krsg::core::PipelineRecord& pipeline,
                               const krsg::ComputeSubmit& submit, const std::uint64_t* bufferBackends);
} // namespace krsg::vulkan
#endif
