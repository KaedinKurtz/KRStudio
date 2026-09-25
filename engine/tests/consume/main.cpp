// Consumer smoke: what any third-party 3D program would do on day one.
#include <cstdio>

#include <krsg/krsg.h>

int main()
{
    const krsg::Version v = krsg::GetVersion();
    if (v.major != KRSG_VERSION_MAJOR || v.minor != KRSG_VERSION_MINOR) {
        std::printf("FAIL: header/binary version mismatch\n");
        return 1;
    }

    krsg::DeviceDesc desc;
    desc.backend = krsg::BackendKind::Null;
    auto device = krsg::Device::Create(desc);
    if (!device.ok()) {
        std::printf("FAIL: null device: %s\n", krsg::ToString(device.code));
        return 1;
    }

    krsg::BufferDesc buffer;
    buffer.size = 256;
    auto handle = device.value->createBuffer(buffer);
    const bool ok = handle.ok() && device.value->isValid(handle.value) &&
                    device.value->destroyBuffer(handle.value) == krsg::ResultCode::Ok;
    krsg::Device::Destroy(device.value);

    std::printf(ok ? "consume OK: krsg %u.%u.%u\n" : "FAIL: buffer round-trip\n", v.major,
                v.minor, v.patch);
    return ok ? 0 : 1;
}
