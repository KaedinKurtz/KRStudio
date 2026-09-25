// GPU execution test (CI stage S4/S5): brings up the real Vulkan backend on
// whatever ICD the runner provides (lavapipe on Linux CI, MoltenVK on macOS)
// and asserts the caps contract. Grows with every WP; any validation-layer
// message is a failure (enforced by the CI wrapper, see CI_PIPELINE.md S4).

#include <cstdio>

#include <krsg/krsg.h>

static int g_debugErrors = 0;

static void debugSink(krsg::Severity severity, const char* message, void*)
{
    std::printf("  [krsg-%d] %s\n", static_cast<int>(severity), message);
    if (severity == krsg::Severity::Error) {
        ++g_debugErrors;
    }
}

int main()
{
    krsg::SetDebugCallback(&debugSink, nullptr);

    krsg::DeviceDesc desc;
    desc.backend = krsg::BackendKind::Vulkan;
    desc.enableValidation = true;
    desc.applicationName = "krsg-gpu-tests";

    auto result = krsg::Device::Create(desc);
    if (!result.ok()) {
        std::printf("FAIL: Vulkan device creation: %s\n", krsg::ToString(result.code));
        return 1;
    }
    krsg::Device* device = result.value;
    const krsg::DeviceCaps& caps = device->caps();
    std::printf("device: '%s' api %u.%u software=%d portability=%d\n", caps.deviceName, caps.apiMajor, caps.apiMinor,
                caps.softwareRasterizer ? 1 : 0, caps.portabilitySubset ? 1 : 0);

    int failures = 0;
    if (caps.backend != krsg::BackendKind::Vulkan) {
        std::printf("FAIL: caps.backend != Vulkan\n");
        ++failures;
    }
    if (caps.apiMajor < 1 || (caps.apiMajor == 1 && caps.apiMinor < 2)) {
        std::printf("FAIL: device below Vulkan 1.2 (capability floor, see WP2 table)\n");
        ++failures;
    }
    if (caps.deviceName[0] == '\0') {
        std::printf("FAIL: empty device name\n");
        ++failures;
    }

    krsg::Device::Destroy(device);
    if (g_debugErrors != 0) {
        std::printf("FAIL: %d krsg error callbacks during run\n", g_debugErrors);
        ++failures;
    }
    std::printf(failures == 0 ? "OK\n" : "FAILED\n");
    return failures == 0 ? 0 : 1;
}
