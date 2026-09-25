#pragma once
// The Device: owner of all GPU objects and the v0 execution path.
//
// Contract highlights (the unit suite in engine/tests/unit is the normative
// spec; the null backend implements it exactly):
//  - Factory-only lifetime: Create()/Destroy(). Destroy() waits for GPU idle,
//    then frees; destroying with live handles is a Warning through the debug
//    sink, never UB.
//  - Every create* validates its descriptor (InvalidArgument) before touching
//    the backend; every handle-taking call detects stale/foreign handles
//    (InvalidHandle) via generational tables.
//  - Host I/O: writeBuffer only on HostVisible, readBuffer only on
//    HostVisible/Readback; DeviceLocal host access is InvalidArgument until
//    the staging ring lands (WP1 continuation — documented, not implied).
//  - submitCompute is synchronous (records, submits, waits): the deterministic
//    bring-up and strict-sync reference path. Frame-paced recording layers on
//    top later; it does not replace this.
// Threading contract v1 (ARCHITECTURE.md §4): resource creation thread-safe is
// NOT yet promised — v0 is single-threaded; widening is an ADR.

#include <krsg/types.h>

namespace krsg
{

class KRSG_API Device
{
public:
    // Factory: the only way to obtain a Device. Destroy() waits for idle first.
    static Result<Device*> Create(const DeviceDesc& desc);
    static void Destroy(Device* device);

    const DeviceCaps& caps() const;

    // --- Buffers ---
    Result<BufferHandle> createBuffer(const BufferDesc& desc);
    ResultCode destroyBuffer(BufferHandle handle);
    bool isValid(BufferHandle handle) const;

    // Host I/O (see contract above). Ranges are validated against the buffer size.
    ResultCode writeBuffer(BufferHandle handle, std::uint64_t offset, const void* data, std::uint64_t size);
    ResultCode readBuffer(BufferHandle handle, std::uint64_t offset, void* out, std::uint64_t size);

    // --- Images & samplers ---
    Result<ImageHandle> createImage(const ImageDesc& desc);
    ResultCode destroyImage(ImageHandle handle);
    bool isValid(ImageHandle handle) const;

    Result<SamplerHandle> createSampler(const SamplerDesc& desc);
    ResultCode destroySampler(SamplerHandle handle);
    bool isValid(SamplerHandle handle) const;

    // --- Shaders & pipelines ---
    // Rejects blobs that do not start with the SPIR-V magic number.
    Result<ShaderHandle> createShaderModule(const ShaderModuleDesc& desc);
    ResultCode destroyShaderModule(ShaderHandle handle);
    bool isValid(ShaderHandle handle) const;

    Result<PipelineHandle> createComputePipeline(const ComputePipelineDesc& desc);
    ResultCode destroyPipeline(PipelineHandle handle);
    bool isValid(PipelineHandle handle) const;

    // --- Execution (v0, synchronous) ---
    // Null backend: validates everything, executes nothing, returns Ok.
    // Vulkan: records bind/push/dispatch + a compute->host barrier, submits,
    // waits on a fence. Results are visible to readBuffer on return.
    ResultCode submitCompute(const ComputeSubmit& submit);

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

protected:
    Device() = default;
    ~Device() = default;
};

} // namespace krsg
