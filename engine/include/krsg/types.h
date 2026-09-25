#pragma once
// Core public types of KRS Graphics. Rules of this header (ARCHITECTURE.md §4):
// no Vulkan, no Qt, no GL, no GLM — opaque handles, POD descriptor structs, and a
// non-throwing Result. Everything backend-specific stays behind the seam.

#include <cstddef>
#include <cstdint>

#include <krsg/version.h>

namespace krsg
{

// ---------------------------------------------------------------------------
// Errors & results
// ---------------------------------------------------------------------------

enum class ResultCode : std::uint32_t {
    Ok = 0,
    Unsupported,     // feature/backend not available on this device or build
    InvalidArgument, // a descriptor failed validation
    InvalidHandle,   // stale, destroyed, or foreign handle
    OutOfMemory,
    DeviceLost,
    Internal,
};

KRSG_API const char* ToString(ResultCode code);

template <typename T> struct Result {
    T value{};
    ResultCode code = ResultCode::Internal;
    bool ok() const { return code == ResultCode::Ok; }
};

enum class Severity : std::uint32_t { Info = 0, Warning, Error };

// Installed once per process; the library never writes to stdio on its own.
using DebugCallback = void (*)(Severity severity, const char* message, void* userData);
KRSG_API void SetDebugCallback(DebugCallback callback, void* userData);

// ---------------------------------------------------------------------------
// Opaque handles (generational; use-after-destroy is detected, not UB)
// ---------------------------------------------------------------------------

namespace detail
{
template <typename Tag> struct Handle {
    std::uint64_t v = 0;
    bool isNull() const { return v == 0; }
    friend bool operator==(Handle a, Handle b) { return a.v == b.v; }
    friend bool operator!=(Handle a, Handle b) { return a.v != b.v; }
};
} // namespace detail

using BufferHandle = detail::Handle<struct BufferTag>;
using ImageHandle = detail::Handle<struct ImageTag>;
using SamplerHandle = detail::Handle<struct SamplerTag>;
using ShaderHandle = detail::Handle<struct ShaderTag>;
using PipelineHandle = detail::Handle<struct PipelineTag>;

// ---------------------------------------------------------------------------
// Device
// ---------------------------------------------------------------------------

enum class BackendKind : std::uint32_t {
    Null = 0, // reference semantics for unit tests and logic-only runs
    Vulkan,
};

struct DeviceDesc {
    BackendKind backend = BackendKind::Vulkan;
    bool enableValidation = false; // Vulkan validation layers, when present
    bool headless = true;          // no surface required; presentation arrives via adapters
    const char* applicationName = "krsg-app";
};

struct DeviceCaps {
    BackendKind backend = BackendKind::Null;
    char deviceName[256] = {};
    std::uint32_t apiMajor = 0;
    std::uint32_t apiMinor = 0;
    bool softwareRasterizer = false; // lavapipe/llvmpipe: perf gates SKIP, correctness gates run
    bool portabilitySubset = false;  // MoltenVK: consult the WP2 capability table
};

// ---------------------------------------------------------------------------
// Resources (v0 surface — grows per work package, additively)
// ---------------------------------------------------------------------------

enum class BufferUsage : std::uint32_t {
    Vertex = 1u << 0,
    Index = 1u << 1,
    Uniform = 1u << 2,
    Storage = 1u << 3,
    Indirect = 1u << 4,
    TransferSrc = 1u << 5,
    TransferDst = 1u << 6,
};
constexpr BufferUsage operator|(BufferUsage a, BufferUsage b)
{
    return static_cast<BufferUsage>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}

enum class MemoryLocation : std::uint32_t {
    DeviceLocal = 0, // GPU-only
    HostVisible,     // persistently mapped upload
    Readback,        // GPU->CPU ring
};

struct BufferDesc {
    std::uint64_t size = 0;
    BufferUsage usage = BufferUsage::Storage;
    MemoryLocation memory = MemoryLocation::DeviceLocal;
    const char* debugName = nullptr;
};

class KRSG_API Device
{
public:
    // Factory: the only way to obtain a Device. Destroy() waits for idle first.
    static Result<Device*> Create(const DeviceDesc& desc);
    static void Destroy(Device* device);

    const DeviceCaps& caps() const;

    Result<BufferHandle> createBuffer(const BufferDesc& desc);
    ResultCode destroyBuffer(BufferHandle handle);
    bool isValid(BufferHandle handle) const;

    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

protected:
    Device() = default;
    ~Device() = default;
};

} // namespace krsg
