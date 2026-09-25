// Public Device facade. Division of labor (device_impl.h): this file owns
// descriptor validation, generational handle bookkeeping, and the null
// backend's reference semantics; src/rhi/vulkan owns GPU objects and
// execution. Every public entry validates BEFORE dispatching, so both
// backends see only well-formed requests — that invariant is what the unit
// suite pins down.

#include <cstring>

#include <krsg/device.h>

#include "core/device_impl.h"

namespace krsg
{

namespace
{

DebugCallback g_debugCallback = nullptr;
void* g_debugUser = nullptr;

constexpr std::uint32_t kSpirvMagic = 0x07230203u;
constexpr std::uint32_t kMaxPushConstantBytes = 128;
constexpr std::uint32_t kMaxStorageBufferBindings = 16;

bool hostReadable(MemoryLocation location)
{
    return location == MemoryLocation::HostVisible || location == MemoryLocation::Readback;
}

} // namespace

void SetDebugCallback(DebugCallback callback, void* userData)
{
    g_debugCallback = callback;
    g_debugUser = userData;
}

namespace core
{

void EmitDebug(Severity severity, const char* message)
{
    if (g_debugCallback != nullptr) {
        g_debugCallback(severity, message, g_debugUser);
    }
}

void CopyName(char* dst, std::size_t capacity, const char* src)
{
    if (dst == nullptr || capacity == 0) {
        return;
    }
    std::size_t i = 0;
    if (src != nullptr) {
        for (; i + 1 < capacity && src[i] != '\0'; ++i) {
            dst[i] = src[i];
        }
    }
    dst[i] = '\0';
}

std::uint32_t FormatTexelSize(Format format)
{
    switch (format) {
    case Format::Unknown:
        return 0;
    case Format::R8Unorm:
        return 1;
    case Format::RG8Unorm:
    case Format::R16Float:
        return 2;
    case Format::RGBA8Unorm:
    case Format::RGBA8Srgb:
    case Format::RG16Float:
    case Format::R32Float:
    case Format::R32Uint:
    case Format::D32Float:
        return 4;
    case Format::RGBA16Float:
    case Format::RG32Float:
    case Format::RG32Uint:
        return 8;
    case Format::RGBA32Float:
    case Format::RGBA32Uint:
        return 16;
    }
    return 0;
}

} // namespace core

const char* ToString(ResultCode code)
{
    switch (code) {
    case ResultCode::Ok:
        return "Ok";
    case ResultCode::Unsupported:
        return "Unsupported";
    case ResultCode::InvalidArgument:
        return "InvalidArgument";
    case ResultCode::InvalidHandle:
        return "InvalidHandle";
    case ResultCode::OutOfMemory:
        return "OutOfMemory";
    case ResultCode::DeviceLost:
        return "DeviceLost";
    case ResultCode::Internal:
        return "Internal";
    }
    return "Unknown";
}

// ---------------------------------------------------------------------------
// Device lifetime
// ---------------------------------------------------------------------------

Result<Device*> Device::Create(const DeviceDesc& desc)
{
    auto* impl = new core::DeviceImpl();
    switch (desc.backend) {
    case BackendKind::Null: {
        impl->capsData.backend = BackendKind::Null;
        core::CopyName(impl->capsData.deviceName, sizeof(impl->capsData.deviceName), "KRS Null Device");
        impl->capsData.softwareRasterizer = true;
        return {impl, ResultCode::Ok};
    }
    case BackendKind::Vulkan: {
#if defined(KRSG_HAS_VULKAN)
        const ResultCode code = vulkan::CreateVulkanDevice(desc, impl);
        if (code != ResultCode::Ok) {
            delete impl;
            return {nullptr, code};
        }
        return {impl, ResultCode::Ok};
#else
        delete impl;
        core::EmitDebug(Severity::Error, "krsg: Vulkan backend not compiled in (KRSG_ENABLE_VULKAN=OFF)");
        return {nullptr, ResultCode::Unsupported};
#endif
    }
    }
    delete impl;
    return {nullptr, ResultCode::InvalidArgument};
}

void Device::Destroy(Device* device)
{
    if (device == nullptr) {
        return;
    }
    auto* impl = static_cast<core::DeviceImpl*>(device);
    const std::size_t live = impl->buffers.liveCount() + impl->images.liveCount() + impl->samplers.liveCount() +
                             impl->shaders.liveCount() + impl->pipelines.liveCount();
    if (live != 0) {
        core::EmitDebug(Severity::Warning, "krsg: Device destroyed with live object handles");
    }
#if defined(KRSG_HAS_VULKAN)
    if (impl->vk != nullptr) {
        vulkan::DestroyVulkanDevice(impl); // waits idle; frees remaining backend objects
    }
#endif
    delete impl;
}

const DeviceCaps& Device::caps() const
{
    return static_cast<const core::DeviceImpl*>(this)->capsData;
}

// ---------------------------------------------------------------------------
// Buffers
// ---------------------------------------------------------------------------

Result<BufferHandle> Device::createBuffer(const BufferDesc& desc)
{
    auto* impl = static_cast<core::DeviceImpl*>(this);
    if (desc.size == 0) {
        core::EmitDebug(Severity::Error, "krsg: BufferDesc.size must be non-zero");
        return {{}, ResultCode::InvalidArgument};
    }
    core::BufferRecord record;
    record.desc = desc;
    if (impl->vk == nullptr) {
        if (hostReadable(desc.memory)) {
            record.hostData.assign(static_cast<std::size_t>(desc.size), 0);
        }
    }
#if defined(KRSG_HAS_VULKAN)
    if (impl->vk != nullptr) {
        const ResultCode code = vulkan::CreateBuffer(impl, record);
        if (code != ResultCode::Ok) {
            return {{}, code};
        }
    }
#endif
    return {{impl->buffers.create(static_cast<core::BufferRecord&&>(record))}, ResultCode::Ok};
}

ResultCode Device::destroyBuffer(BufferHandle handle)
{
    auto* impl = static_cast<core::DeviceImpl*>(this);
    core::BufferRecord* record = impl->buffers.get(handle.v);
    if (record == nullptr) {
        return ResultCode::InvalidHandle;
    }
#if defined(KRSG_HAS_VULKAN)
    if (impl->vk != nullptr) {
        vulkan::DestroyBuffer(impl, *record);
    }
#endif
    impl->buffers.destroy(handle.v);
    return ResultCode::Ok;
}

bool Device::isValid(BufferHandle handle) const
{
    return static_cast<const core::DeviceImpl*>(this)->buffers.isValid(handle.v);
}

ResultCode Device::writeBuffer(BufferHandle handle, std::uint64_t offset, const void* data, std::uint64_t size)
{
    auto* impl = static_cast<core::DeviceImpl*>(this);
    core::BufferRecord* record = impl->buffers.get(handle.v);
    if (record == nullptr) {
        return ResultCode::InvalidHandle;
    }
    if (data == nullptr || size == 0 || offset + size > record->desc.size) {
        core::EmitDebug(Severity::Error, "krsg: writeBuffer range/data invalid");
        return ResultCode::InvalidArgument;
    }
    if (record->desc.memory != MemoryLocation::HostVisible) {
        core::EmitDebug(Severity::Error, "krsg: writeBuffer requires HostVisible memory (no staging path in v0)");
        return ResultCode::InvalidArgument;
    }
#if defined(KRSG_HAS_VULKAN)
    if (impl->vk != nullptr) {
        return vulkan::WriteBuffer(impl, *record, offset, data, size);
    }
#endif
    std::memcpy(record->hostData.data() + offset, data, static_cast<std::size_t>(size));
    return ResultCode::Ok;
}

ResultCode Device::readBuffer(BufferHandle handle, std::uint64_t offset, void* out, std::uint64_t size)
{
    auto* impl = static_cast<core::DeviceImpl*>(this);
    core::BufferRecord* record = impl->buffers.get(handle.v);
    if (record == nullptr) {
        return ResultCode::InvalidHandle;
    }
    if (out == nullptr || size == 0 || offset + size > record->desc.size) {
        core::EmitDebug(Severity::Error, "krsg: readBuffer range/output invalid");
        return ResultCode::InvalidArgument;
    }
    if (!hostReadable(record->desc.memory)) {
        core::EmitDebug(Severity::Error, "krsg: readBuffer requires HostVisible/Readback memory");
        return ResultCode::InvalidArgument;
    }
#if defined(KRSG_HAS_VULKAN)
    if (impl->vk != nullptr) {
        return vulkan::ReadBuffer(impl, *record, offset, out, size);
    }
#endif
    std::memcpy(out, record->hostData.data() + offset, static_cast<std::size_t>(size));
    return ResultCode::Ok;
}

// ---------------------------------------------------------------------------
// Images & samplers
// ---------------------------------------------------------------------------

Result<ImageHandle> Device::createImage(const ImageDesc& desc)
{
    auto* impl = static_cast<core::DeviceImpl*>(this);
    const bool extentBad =
        desc.width == 0 || desc.height == 0 || desc.depth == 0 || (desc.type == ImageType::Tex2D && desc.depth != 1);
    if (extentBad || desc.mipLevels == 0 || core::FormatTexelSize(desc.format) == 0) {
        core::EmitDebug(Severity::Error, "krsg: ImageDesc extent/mips/format invalid");
        return {{}, ResultCode::InvalidArgument};
    }
    core::ImageRecord record;
    record.desc = desc;
#if defined(KRSG_HAS_VULKAN)
    if (impl->vk != nullptr) {
        const ResultCode code = vulkan::CreateImage(impl, record);
        if (code != ResultCode::Ok) {
            return {{}, code};
        }
    }
#endif
    return {{impl->images.create(static_cast<core::ImageRecord&&>(record))}, ResultCode::Ok};
}

ResultCode Device::destroyImage(ImageHandle handle)
{
    auto* impl = static_cast<core::DeviceImpl*>(this);
    core::ImageRecord* record = impl->images.get(handle.v);
    if (record == nullptr) {
        return ResultCode::InvalidHandle;
    }
#if defined(KRSG_HAS_VULKAN)
    if (impl->vk != nullptr) {
        vulkan::DestroyImage(impl, *record);
    }
#endif
    impl->images.destroy(handle.v);
    return ResultCode::Ok;
}

bool Device::isValid(ImageHandle handle) const
{
    return static_cast<const core::DeviceImpl*>(this)->images.isValid(handle.v);
}

Result<SamplerHandle> Device::createSampler(const SamplerDesc& desc)
{
    auto* impl = static_cast<core::DeviceImpl*>(this);
    core::SamplerRecord record;
    record.desc = desc;
#if defined(KRSG_HAS_VULKAN)
    if (impl->vk != nullptr) {
        const ResultCode code = vulkan::CreateSampler(impl, record);
        if (code != ResultCode::Ok) {
            return {{}, code};
        }
    }
#endif
    return {{impl->samplers.create(static_cast<core::SamplerRecord&&>(record))}, ResultCode::Ok};
}

ResultCode Device::destroySampler(SamplerHandle handle)
{
    auto* impl = static_cast<core::DeviceImpl*>(this);
    core::SamplerRecord* record = impl->samplers.get(handle.v);
    if (record == nullptr) {
        return ResultCode::InvalidHandle;
    }
#if defined(KRSG_HAS_VULKAN)
    if (impl->vk != nullptr) {
        vulkan::DestroySampler(impl, *record);
    }
#endif
    impl->samplers.destroy(handle.v);
    return ResultCode::Ok;
}

bool Device::isValid(SamplerHandle handle) const
{
    return static_cast<const core::DeviceImpl*>(this)->samplers.isValid(handle.v);
}

// ---------------------------------------------------------------------------
// Shaders & pipelines
// ---------------------------------------------------------------------------

Result<ShaderHandle> Device::createShaderModule(const ShaderModuleDesc& desc)
{
    auto* impl = static_cast<core::DeviceImpl*>(this);
    if (desc.spirv == nullptr || desc.wordCount < 5 || desc.spirv[0] != kSpirvMagic) {
        core::EmitDebug(Severity::Error, "krsg: ShaderModuleDesc is not a SPIR-V blob");
        return {{}, ResultCode::InvalidArgument};
    }
    core::ShaderRecord record;
    record.wordCount = static_cast<std::uint32_t>(desc.wordCount);
#if defined(KRSG_HAS_VULKAN)
    if (impl->vk != nullptr) {
        const ResultCode code = vulkan::CreateShaderModule(impl, record, desc);
        if (code != ResultCode::Ok) {
            return {{}, code};
        }
    }
#endif
    return {{impl->shaders.create(static_cast<core::ShaderRecord&&>(record))}, ResultCode::Ok};
}

ResultCode Device::destroyShaderModule(ShaderHandle handle)
{
    auto* impl = static_cast<core::DeviceImpl*>(this);
    core::ShaderRecord* record = impl->shaders.get(handle.v);
    if (record == nullptr) {
        return ResultCode::InvalidHandle;
    }
#if defined(KRSG_HAS_VULKAN)
    if (impl->vk != nullptr) {
        vulkan::DestroyShaderModule(impl, *record);
    }
#endif
    impl->shaders.destroy(handle.v);
    return ResultCode::Ok;
}

bool Device::isValid(ShaderHandle handle) const
{
    return static_cast<const core::DeviceImpl*>(this)->shaders.isValid(handle.v);
}

Result<PipelineHandle> Device::createComputePipeline(const ComputePipelineDesc& desc)
{
    auto* impl = static_cast<core::DeviceImpl*>(this);
    core::ShaderRecord* shader = impl->shaders.get(desc.shader.v);
    if (shader == nullptr) {
        return {{}, ResultCode::InvalidHandle};
    }
    if (desc.entryPoint == nullptr || desc.pushConstantSize > kMaxPushConstantBytes ||
        desc.storageBufferBindings > kMaxStorageBufferBindings) {
        core::EmitDebug(Severity::Error, "krsg: ComputePipelineDesc invalid "
                                         "(entry/pushConstantSize<=128/bindings<=16)");
        return {{}, ResultCode::InvalidArgument};
    }
    core::PipelineRecord record;
    record.desc = desc;
    record.desc.entryPoint = nullptr; // not retained; backend consumes it at creation
    record.desc.debugName = nullptr;
#if defined(KRSG_HAS_VULKAN)
    if (impl->vk != nullptr) {
        const ResultCode code = vulkan::CreateComputePipeline(impl, record, desc, shader->backend);
        if (code != ResultCode::Ok) {
            return {{}, code};
        }
    }
#endif
    return {{impl->pipelines.create(static_cast<core::PipelineRecord&&>(record))}, ResultCode::Ok};
}

ResultCode Device::destroyPipeline(PipelineHandle handle)
{
    auto* impl = static_cast<core::DeviceImpl*>(this);
    core::PipelineRecord* record = impl->pipelines.get(handle.v);
    if (record == nullptr) {
        return ResultCode::InvalidHandle;
    }
#if defined(KRSG_HAS_VULKAN)
    if (impl->vk != nullptr) {
        vulkan::DestroyPipeline(impl, *record);
    }
#endif
    impl->pipelines.destroy(handle.v);
    return ResultCode::Ok;
}

bool Device::isValid(PipelineHandle handle) const
{
    return static_cast<const core::DeviceImpl*>(this)->pipelines.isValid(handle.v);
}

// ---------------------------------------------------------------------------
// Execution (v0 synchronous)
// ---------------------------------------------------------------------------

ResultCode Device::submitCompute(const ComputeSubmit& submit)
{
    auto* impl = static_cast<core::DeviceImpl*>(this);
    core::PipelineRecord* pipeline = impl->pipelines.get(submit.pipeline.v);
    if (pipeline == nullptr) {
        return ResultCode::InvalidHandle;
    }
    if (submit.bufferCount != pipeline->desc.storageBufferBindings ||
        (submit.bufferCount != 0 && submit.buffers == nullptr)) {
        core::EmitDebug(Severity::Error, "krsg: submitCompute buffer bindings do not match the pipeline layout");
        return ResultCode::InvalidArgument;
    }
    if (submit.pushConstantSize != pipeline->desc.pushConstantSize ||
        (submit.pushConstantSize != 0 && submit.pushConstants == nullptr)) {
        core::EmitDebug(Severity::Error, "krsg: submitCompute push constants do not match the pipeline layout");
        return ResultCode::InvalidArgument;
    }
    if (submit.groupsX == 0 || submit.groupsY == 0 || submit.groupsZ == 0) {
        core::EmitDebug(Severity::Error, "krsg: submitCompute dispatch groups must be non-zero");
        return ResultCode::InvalidArgument;
    }
    std::uint64_t bufferBackends[kMaxStorageBufferBindings] = {};
    for (std::uint32_t i = 0; i < submit.bufferCount; ++i) {
        core::BufferRecord* buffer = impl->buffers.get(submit.buffers[i].v);
        if (buffer == nullptr) {
            return ResultCode::InvalidHandle;
        }
        if (!HasUsage(buffer->desc.usage, BufferUsage::Storage)) {
            core::EmitDebug(Severity::Error, "krsg: submitCompute binding lacks BufferUsage::Storage");
            return ResultCode::InvalidArgument;
        }
        bufferBackends[i] = buffer->backend;
    }
#if defined(KRSG_HAS_VULKAN)
    if (impl->vk != nullptr) {
        return vulkan::SubmitCompute(impl, *pipeline, submit, bufferBackends);
    }
#endif
    (void)bufferBackends;
    // Null backend: everything validated; execution is a defined no-op.
    return ResultCode::Ok;
}

} // namespace krsg
