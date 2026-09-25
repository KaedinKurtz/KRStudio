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
// Vulkan validation-layer messages are forwarded through this sink (Warning/
// Error severity), which is how CI turns validation findings into test failures.
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
// Formats (grown deliberately: every entry is used by the engine or the app
// port — see the WP2 capability table before adding one)
// ---------------------------------------------------------------------------

enum class Format : std::uint32_t {
    Unknown = 0,
    // Color, 8-bit
    R8Unorm,
    RG8Unorm,
    RGBA8Unorm,
    RGBA8Srgb,
    // Color, float
    R16Float,
    RG16Float,
    RGBA16Float, // the app's G-buffer / finalColor format
    R32Float,    // SDF volumes (filterability is a WP2 capability check)
    RG32Float,
    RGBA32Float, // LTC matrix tables
    // Integer (direct Vulkan keeps these; QRhi could not)
    R32Uint,  // caustics accumulation
    RG32Uint, // the pick target, ported as-is
    RGBA32Uint,
    // Depth
    D32Float, // the app's depth attachment
};

// ---------------------------------------------------------------------------
// Buffers
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
constexpr bool HasUsage(BufferUsage value, BufferUsage flag)
{
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
}

enum class MemoryLocation : std::uint32_t {
    DeviceLocal = 0, // GPU-only; host I/O requires a staging path (not in v0)
    HostVisible,     // persistently mapped upload; Device::writeBuffer works
    Readback,        // GPU->CPU; Device::readBuffer works
};

struct BufferDesc {
    std::uint64_t size = 0;
    BufferUsage usage = BufferUsage::Storage;
    MemoryLocation memory = MemoryLocation::DeviceLocal;
    const char* debugName = nullptr;
};

// ---------------------------------------------------------------------------
// Images & samplers
// ---------------------------------------------------------------------------

enum class ImageType : std::uint32_t { Tex2D = 0, Tex3D };

enum class ImageUsage : std::uint32_t {
    Sampled = 1u << 0,
    Storage = 1u << 1, // image load/store (the compute pipelines live here)
    ColorAttachment = 1u << 2,
    DepthAttachment = 1u << 3,
    TransferSrc = 1u << 4,
    TransferDst = 1u << 5,
};
constexpr ImageUsage operator|(ImageUsage a, ImageUsage b)
{
    return static_cast<ImageUsage>(static_cast<std::uint32_t>(a) | static_cast<std::uint32_t>(b));
}
constexpr bool HasUsage(ImageUsage value, ImageUsage flag)
{
    return (static_cast<std::uint32_t>(value) & static_cast<std::uint32_t>(flag)) != 0;
}

struct ImageDesc {
    ImageType type = ImageType::Tex2D;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t depth = 1; // Tex3D only; must be 1 for Tex2D
    std::uint32_t mipLevels = 1;
    Format format = Format::Unknown;
    ImageUsage usage = ImageUsage::Sampled;
    const char* debugName = nullptr;
};

enum class Filter : std::uint32_t { Nearest = 0, Linear };
enum class AddressMode : std::uint32_t { Repeat = 0, ClampToEdge, ClampToBorder };

struct SamplerDesc {
    Filter minFilter = Filter::Linear;
    Filter magFilter = Filter::Linear;
    Filter mipFilter = Filter::Linear;
    AddressMode addressU = AddressMode::ClampToEdge;
    AddressMode addressV = AddressMode::ClampToEdge;
    AddressMode addressW = AddressMode::ClampToEdge;
    const char* debugName = nullptr;
};

// ---------------------------------------------------------------------------
// Shaders & pipelines
// ---------------------------------------------------------------------------

// SPIR-V only. The words are copied at creation; the caller keeps ownership of
// the input. Offline compilation is the contract (glslang in the shader gate /
// build; no runtime GLSL compiler ships — IMPLEMENTATION_PLAN.md WP4).
struct ShaderModuleDesc {
    const std::uint32_t* spirv = nullptr;
    std::size_t wordCount = 0;
    const char* debugName = nullptr;
};

// v0 compute pipeline: one descriptor set of storage buffers (the sim kernels'
// shape) + optional push constants. Graphics pipelines arrive with the render
// graph (WP3) — not speculatively designed here.
struct ComputePipelineDesc {
    ShaderHandle shader{};
    const char* entryPoint = "main";
    std::uint32_t storageBufferBindings = 0; // bindings 0..N-1, set 0
    std::uint32_t pushConstantSize = 0;      // bytes, 0..128
    const char* debugName = nullptr;
};

// One synchronous compute submission: bind, push, dispatch, wait. This is the
// deterministic bring-up path (and the strict-sync reference mode the plan
// requires); the recorded multi-pass Frame API layers on top of it later in
// WP1 without changing these types.
struct ComputeSubmit {
    PipelineHandle pipeline{};
    const BufferHandle* buffers = nullptr; // one per binding 0..count-1
    std::uint32_t bufferCount = 0;
    const void* pushConstants = nullptr; // pushConstantSize bytes, or null
    std::uint32_t pushConstantSize = 0;
    std::uint32_t groupsX = 1;
    std::uint32_t groupsY = 1;
    std::uint32_t groupsZ = 1;
};

// ---------------------------------------------------------------------------
// Device description & capabilities
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

} // namespace krsg
