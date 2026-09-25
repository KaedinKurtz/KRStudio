// GPU execution tests (CI stage S4 lavapipe / S5 MoltenVK): the real Vulkan
// backend on whatever ICD the runner provides. Two tests:
//   1) device bring-up + caps contract;
//   2) end-to-end compute checksum — buffer upload, SPIR-V pipeline, push
//      constants, dispatch, compute->host barrier, readback, exact compare.
// Validation layers are requested; any Error-severity debug-sink callback
// (which includes forwarded validation messages) fails the run.

#include <cstdio>
#include <vector>

#include <krsg/krsg.h>

#include "checksum_spirv.h"

static int g_debugErrors = 0;

static void debugSink(krsg::Severity severity, const char* message, void*)
{
    std::printf("  [krsg-%d] %s\n", static_cast<int>(severity), message);
    if (severity == krsg::Severity::Error) {
        ++g_debugErrors;
    }
}

static int testDeviceCaps(krsg::Device* device)
{
    int failures = 0;
    const krsg::DeviceCaps& caps = device->caps();
    std::printf("device: '%s' api %u.%u software=%d portability=%d\n", caps.deviceName, caps.apiMajor, caps.apiMinor,
                caps.softwareRasterizer ? 1 : 0, caps.portabilitySubset ? 1 : 0);
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
    return failures;
}

static int testComputeChecksum(krsg::Device* device)
{
    constexpr std::uint32_t kElements = 4096;
    constexpr std::uint32_t kSeed = 0x5EED0001u;
    constexpr std::uint64_t kBytes = kElements * sizeof(std::uint32_t);
    int failures = 0;

    krsg::BufferDesc inDesc;
    inDesc.size = kBytes;
    inDesc.usage = krsg::BufferUsage::Storage;
    inDesc.memory = krsg::MemoryLocation::HostVisible;
    inDesc.debugName = "checksum-in";
    auto inBuf = device->createBuffer(inDesc);

    krsg::BufferDesc outDesc = inDesc;
    outDesc.memory = krsg::MemoryLocation::Readback;
    outDesc.debugName = "checksum-out";
    auto outBuf = device->createBuffer(outDesc);
    if (!inBuf.ok() || !outBuf.ok()) {
        std::printf("FAIL: buffer creation (%s / %s)\n", krsg::ToString(inBuf.code), krsg::ToString(outBuf.code));
        return 1;
    }

    std::vector<std::uint32_t> input(kElements);
    for (std::uint32_t i = 0; i < kElements; ++i) {
        input[i] = i * 2654435761u; // Knuth hash spread — not a trivial pattern
    }
    if (device->writeBuffer(inBuf.value, 0, input.data(), kBytes) != krsg::ResultCode::Ok) {
        std::printf("FAIL: writeBuffer\n");
        return 1;
    }

    krsg::ShaderModuleDesc shaderDesc;
    shaderDesc.spirv = krsg_test::kChecksumSpirv;
    shaderDesc.wordCount = krsg_test::kChecksumSpirvWords;
    shaderDesc.debugName = "test_checksum.comp";
    auto shader = device->createShaderModule(shaderDesc);
    if (!shader.ok()) {
        std::printf("FAIL: createShaderModule: %s\n", krsg::ToString(shader.code));
        return 1;
    }

    krsg::ComputePipelineDesc pipeDesc;
    pipeDesc.shader = shader.value;
    pipeDesc.storageBufferBindings = 2;
    pipeDesc.pushConstantSize = 8;
    pipeDesc.debugName = "checksum";
    auto pipeline = device->createComputePipeline(pipeDesc);
    if (!pipeline.ok()) {
        std::printf("FAIL: createComputePipeline: %s\n", krsg::ToString(pipeline.code));
        return 1;
    }

    const krsg::BufferHandle buffers[2] = {inBuf.value, outBuf.value};
    const std::uint32_t push[2] = {kElements, kSeed};
    krsg::ComputeSubmit submit;
    submit.pipeline = pipeline.value;
    submit.buffers = buffers;
    submit.bufferCount = 2;
    submit.pushConstants = push;
    submit.pushConstantSize = 8;
    submit.groupsX = (kElements + 63) / 64;
    if (device->submitCompute(submit) != krsg::ResultCode::Ok) {
        std::printf("FAIL: submitCompute\n");
        return 1;
    }

    std::vector<std::uint32_t> output(kElements, 0);
    if (device->readBuffer(outBuf.value, 0, output.data(), kBytes) != krsg::ResultCode::Ok) {
        std::printf("FAIL: readBuffer\n");
        return 1;
    }
    std::uint32_t mismatches = 0;
    for (std::uint32_t i = 0; i < kElements; ++i) {
        const std::uint32_t expected = input[i] * 3u + i + kSeed;
        if (output[i] != expected && mismatches++ < 4) {
            std::printf("FAIL: element %u: got 0x%08x want 0x%08x\n", i, output[i], expected);
        }
    }
    if (mismatches != 0) {
        std::printf("FAIL: %u/%u mismatched elements\n", mismatches, kElements);
        ++failures;
    } else {
        std::printf("checksum: %u elements exact-match after GPU dispatch\n", kElements);
    }

    device->destroyPipeline(pipeline.value);
    device->destroyShaderModule(shader.value);
    device->destroyBuffer(inBuf.value);
    device->destroyBuffer(outBuf.value);
    return failures;
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

    int failures = 0;
    failures += testDeviceCaps(device);
    failures += testComputeChecksum(device);

    krsg::Device::Destroy(device);
    if (g_debugErrors != 0) {
        std::printf("FAIL: %d krsg/validation error callbacks during run\n", g_debugErrors);
        ++failures;
    }
    std::printf(failures == 0 ? "OK\n" : "FAILED\n");
    return failures == 0 ? 0 : 1;
}
