// Public Device facade: validation + handle bookkeeping live here; backend
// specifics live behind the dispatch below. WP0 note, stated honestly: buffer
// objects are records only (no GPU memory) until WP1 lands VMA — the tests that
// exist today assert handle semantics, not allocation.

#include <cstring>

#include <krsg/types.h>

#include "core/device_impl.h"

namespace krsg
{

namespace
{
DebugCallback g_debugCallback = nullptr;
void* g_debugUser = nullptr;
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

Result<Device*> Device::Create(const DeviceDesc& desc)
{
    auto* impl = new core::DeviceImpl();
    switch (desc.backend) {
    case BackendKind::Null: {
        impl->capsData.backend = BackendKind::Null;
        std::strncpy(impl->capsData.deviceName, "KRS Null Device", sizeof(impl->capsData.deviceName) - 1);
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
    if (impl->buffers.liveCount() != 0) {
        core::EmitDebug(Severity::Warning, "krsg: Device destroyed with live buffer handles");
    }
#if defined(KRSG_HAS_VULKAN)
    if (impl->vk != nullptr) {
        vulkan::DestroyVulkanDevice(impl);
    }
#endif
    delete impl;
}

const DeviceCaps& Device::caps() const
{
    return static_cast<const core::DeviceImpl*>(this)->capsData;
}

Result<BufferHandle> Device::createBuffer(const BufferDesc& desc)
{
    auto* impl = static_cast<core::DeviceImpl*>(this);
    if (desc.size == 0) {
        core::EmitDebug(Severity::Error, "krsg: BufferDesc.size must be non-zero");
        return {{}, ResultCode::InvalidArgument};
    }
    core::BufferRecord record;
    record.desc = desc;
    return {{impl->buffers.create(static_cast<core::BufferRecord&&>(record))}, ResultCode::Ok};
}

ResultCode Device::destroyBuffer(BufferHandle handle)
{
    auto* impl = static_cast<core::DeviceImpl*>(this);
    return impl->buffers.destroy(handle.v) ? ResultCode::Ok : ResultCode::InvalidHandle;
}

bool Device::isValid(BufferHandle handle) const
{
    return static_cast<const core::DeviceImpl*>(this)->buffers.isValid(handle.v);
}

} // namespace krsg
