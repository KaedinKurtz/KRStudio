// API-contract tests against the null backend (CI stage S3). These are the
// reference semantics every real backend must match.

#include <krsg/krsg.h>

#include "test_framework.h"

using krsg::BackendKind;
using krsg::BufferDesc;
using krsg::BufferHandle;
using krsg::Device;
using krsg::DeviceDesc;
using krsg::ResultCode;

namespace
{
Device* makeNullDevice()
{
    DeviceDesc desc;
    desc.backend = BackendKind::Null;
    auto result = Device::Create(desc);
    CHECK(result.ok());
    return result.value;
}
} // namespace

KRSG_TEST(version_macros_match_runtime)
{
    const krsg::Version v = krsg::GetVersion();
    CHECK(v.major == KRSG_VERSION_MAJOR);
    CHECK(v.minor == KRSG_VERSION_MINOR);
    CHECK(v.patch == KRSG_VERSION_PATCH);
}

KRSG_TEST(null_device_creates_with_caps)
{
    Device* device = makeNullDevice();
    CHECK(device != nullptr);
    CHECK(device->caps().backend == BackendKind::Null);
    CHECK(device->caps().softwareRasterizer);
    Device::Destroy(device);
}

KRSG_TEST(buffer_lifecycle_and_stale_handles)
{
    Device* device = makeNullDevice();

    BufferDesc desc;
    desc.size = 1024;
    auto created = device->createBuffer(desc);
    CHECK(created.ok());
    const BufferHandle handle = created.value;
    CHECK(!handle.isNull());
    CHECK(device->isValid(handle));

    CHECK(device->destroyBuffer(handle) == ResultCode::Ok);
    CHECK(!device->isValid(handle));
    // Double destroy is detected, not UB.
    CHECK(device->destroyBuffer(handle) == ResultCode::InvalidHandle);

    // A recycled slot must not resurrect the old handle (generational check).
    auto second = device->createBuffer(desc);
    CHECK(second.ok());
    CHECK(second.value != handle);
    CHECK(!device->isValid(handle));
    CHECK(device->isValid(second.value));
    CHECK(device->destroyBuffer(second.value) == ResultCode::Ok);

    Device::Destroy(device);
}

KRSG_TEST(zero_size_buffer_rejected)
{
    Device* device = makeNullDevice();
    BufferDesc desc;
    desc.size = 0;
    auto created = device->createBuffer(desc);
    CHECK(!created.ok());
    CHECK(created.code == ResultCode::InvalidArgument);
    Device::Destroy(device);
}

KRSG_TEST(null_handle_is_never_valid)
{
    Device* device = makeNullDevice();
    CHECK(!device->isValid(BufferHandle{}));
    CHECK(device->destroyBuffer(BufferHandle{}) == ResultCode::InvalidHandle);
    Device::Destroy(device);
}

int main()
{
    return krsg::test::RunAll();
}
