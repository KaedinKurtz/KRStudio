// WP1 contract tests: images, samplers, shader modules, pipelines, host I/O,
// and submitCompute validation — all against the null backend, whose behavior
// IS the normative spec for the Vulkan backend (CI stage S3).

#include <cstring>

#include <krsg/krsg.h>

#include "test_framework.h"

using namespace krsg;

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

// A minimal structurally-valid SPIR-V header (magic, version 1.5, generator,
// bound, schema) — enough for facade validation; real modules are GPU-tested.
const std::uint32_t kTinySpirv[5] = {0x07230203u, 0x00010500u, 0, 1, 0};
} // namespace

KRSG_TEST(host_buffer_write_read_roundtrip)
{
    Device* device = makeNullDevice();
    BufferDesc desc;
    desc.size = 64;
    desc.usage = BufferUsage::Storage;
    desc.memory = MemoryLocation::HostVisible;
    auto buffer = device->createBuffer(desc);
    CHECK(buffer.ok());

    std::uint32_t in[16];
    for (std::uint32_t i = 0; i < 16; ++i) {
        in[i] = i * 3u + 1u;
    }
    CHECK(device->writeBuffer(buffer.value, 0, in, sizeof(in)) == ResultCode::Ok);

    std::uint32_t out[16] = {};
    CHECK(device->readBuffer(buffer.value, 0, out, sizeof(out)) == ResultCode::Ok);
    CHECK(std::memcmp(in, out, sizeof(in)) == 0);

    // Offset-window read.
    std::uint32_t tail[4] = {};
    CHECK(device->readBuffer(buffer.value, 48, tail, 16) == ResultCode::Ok);
    CHECK(tail[0] == in[12] && tail[3] == in[15]);

    CHECK(device->destroyBuffer(buffer.value) == ResultCode::Ok);
    Device::Destroy(device);
}

KRSG_TEST(host_io_rules_enforced)
{
    Device* device = makeNullDevice();
    BufferDesc localDesc;
    localDesc.size = 64;
    localDesc.memory = MemoryLocation::DeviceLocal;
    auto local = device->createBuffer(localDesc);
    CHECK(local.ok());

    std::uint32_t word = 7;
    // DeviceLocal host access: rejected until the staging path exists (documented).
    CHECK(device->writeBuffer(local.value, 0, &word, 4) == ResultCode::InvalidArgument);
    CHECK(device->readBuffer(local.value, 0, &word, 4) == ResultCode::InvalidArgument);

    BufferDesc hostDesc;
    hostDesc.size = 16;
    hostDesc.memory = MemoryLocation::HostVisible;
    auto host = device->createBuffer(hostDesc);
    CHECK(host.ok());
    // Out-of-range and null-pointer writes are rejected.
    CHECK(device->writeBuffer(host.value, 12, &word, 8) == ResultCode::InvalidArgument);
    CHECK(device->writeBuffer(host.value, 0, nullptr, 4) == ResultCode::InvalidArgument);
    // Stale handle after destroy.
    CHECK(device->destroyBuffer(host.value) == ResultCode::Ok);
    CHECK(device->writeBuffer(host.value, 0, &word, 4) == ResultCode::InvalidHandle);

    CHECK(device->destroyBuffer(local.value) == ResultCode::Ok);
    Device::Destroy(device);
}

KRSG_TEST(image_lifecycle_and_validation)
{
    Device* device = makeNullDevice();
    ImageDesc desc;
    desc.width = 128;
    desc.height = 64;
    desc.format = Format::RGBA16Float;
    desc.usage = ImageUsage::Sampled | ImageUsage::ColorAttachment;
    auto image = device->createImage(desc);
    CHECK(image.ok());
    CHECK(device->isValid(image.value));
    CHECK(device->destroyImage(image.value) == ResultCode::Ok);
    CHECK(!device->isValid(image.value));
    CHECK(device->destroyImage(image.value) == ResultCode::InvalidHandle);

    ImageDesc bad = desc;
    bad.width = 0;
    CHECK(!device->createImage(bad).ok());
    bad = desc;
    bad.format = Format::Unknown;
    CHECK(!device->createImage(bad).ok());
    bad = desc;
    bad.depth = 4; // depth>1 on a Tex2D
    CHECK(!device->createImage(bad).ok());

    ImageDesc volume;
    volume.type = ImageType::Tex3D;
    volume.width = 32;
    volume.height = 32;
    volume.depth = 32;
    volume.format = Format::R32Float;
    volume.usage = ImageUsage::Storage;
    auto tex3d = device->createImage(volume);
    CHECK(tex3d.ok());
    CHECK(device->destroyImage(tex3d.value) == ResultCode::Ok);
    Device::Destroy(device);
}

KRSG_TEST(sampler_lifecycle)
{
    Device* device = makeNullDevice();
    SamplerDesc desc;
    desc.minFilter = Filter::Nearest;
    auto sampler = device->createSampler(desc);
    CHECK(sampler.ok());
    CHECK(device->isValid(sampler.value));
    CHECK(device->destroySampler(sampler.value) == ResultCode::Ok);
    CHECK(device->destroySampler(sampler.value) == ResultCode::InvalidHandle);
    Device::Destroy(device);
}

KRSG_TEST(shader_module_rejects_non_spirv)
{
    Device* device = makeNullDevice();
    ShaderModuleDesc good;
    good.spirv = kTinySpirv;
    good.wordCount = 5;
    auto shader = device->createShaderModule(good);
    CHECK(shader.ok());

    ShaderModuleDesc badMagic = good;
    const std::uint32_t notSpirv[5] = {0xDEADBEEFu, 0, 0, 0, 0};
    badMagic.spirv = notSpirv;
    CHECK(device->createShaderModule(badMagic).code == ResultCode::InvalidArgument);

    ShaderModuleDesc tooShort = good;
    tooShort.wordCount = 2;
    CHECK(device->createShaderModule(tooShort).code == ResultCode::InvalidArgument);

    CHECK(device->destroyShaderModule(shader.value) == ResultCode::Ok);
    Device::Destroy(device);
}

KRSG_TEST(compute_pipeline_and_submit_validation)
{
    Device* device = makeNullDevice();
    ShaderModuleDesc shaderDesc;
    shaderDesc.spirv = kTinySpirv;
    shaderDesc.wordCount = 5;
    auto shader = device->createShaderModule(shaderDesc);
    CHECK(shader.ok());

    ComputePipelineDesc pipeDesc;
    pipeDesc.shader = shader.value;
    pipeDesc.storageBufferBindings = 2;
    pipeDesc.pushConstantSize = 8;
    auto pipeline = device->createComputePipeline(pipeDesc);
    CHECK(pipeline.ok());

    // Layout limits enforced.
    ComputePipelineDesc tooBig = pipeDesc;
    tooBig.pushConstantSize = 256;
    CHECK(device->createComputePipeline(tooBig).code == ResultCode::InvalidArgument);
    ComputePipelineDesc staleShader = pipeDesc;
    staleShader.shader = ShaderHandle{};
    CHECK(device->createComputePipeline(staleShader).code == ResultCode::InvalidHandle);

    BufferDesc bufDesc;
    bufDesc.size = 256;
    bufDesc.usage = BufferUsage::Storage;
    bufDesc.memory = MemoryLocation::HostVisible;
    auto bufA = device->createBuffer(bufDesc);
    auto bufB = device->createBuffer(bufDesc);
    CHECK(bufA.ok() && bufB.ok());

    const BufferHandle buffers[2] = {bufA.value, bufB.value};
    const std::uint32_t push[2] = {64, 0};

    ComputeSubmit submit;
    submit.pipeline = pipeline.value;
    submit.buffers = buffers;
    submit.bufferCount = 2;
    submit.pushConstants = push;
    submit.pushConstantSize = 8;
    submit.groupsX = 1;
    CHECK(device->submitCompute(submit) == ResultCode::Ok);

    // Mismatched binding count / push size / zero groups / stale buffer.
    ComputeSubmit wrongCount = submit;
    wrongCount.bufferCount = 1;
    CHECK(device->submitCompute(wrongCount) == ResultCode::InvalidArgument);
    ComputeSubmit wrongPush = submit;
    wrongPush.pushConstantSize = 4;
    CHECK(device->submitCompute(wrongPush) == ResultCode::InvalidArgument);
    ComputeSubmit zeroGroups = submit;
    zeroGroups.groupsX = 0;
    CHECK(device->submitCompute(zeroGroups) == ResultCode::InvalidArgument);
    CHECK(device->destroyBuffer(bufB.value) == ResultCode::Ok);
    CHECK(device->submitCompute(submit) == ResultCode::InvalidHandle);

    // Non-storage buffer bound to a storage slot.
    BufferDesc vertexDesc;
    vertexDesc.size = 256;
    vertexDesc.usage = BufferUsage::Vertex;
    auto vertexBuf = device->createBuffer(vertexDesc);
    CHECK(vertexBuf.ok());
    const BufferHandle wrongUsage[2] = {bufA.value, vertexBuf.value};
    ComputeSubmit badUsage = submit;
    badUsage.buffers = wrongUsage;
    CHECK(device->submitCompute(badUsage) == ResultCode::InvalidArgument);

    CHECK(device->destroyBuffer(vertexBuf.value) == ResultCode::Ok);
    CHECK(device->destroyBuffer(bufA.value) == ResultCode::Ok);
    CHECK(device->destroyPipeline(pipeline.value) == ResultCode::Ok);
    CHECK(device->destroyShaderModule(shader.value) == ResultCode::Ok);
    Device::Destroy(device);
}
