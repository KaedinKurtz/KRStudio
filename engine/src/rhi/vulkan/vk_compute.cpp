// Vulkan backend: compute pipelines and the v0 synchronous submit path.
// Layout convention (IMPLEMENTATION_PLAN.md, shader changeover rule 2): one
// descriptor set of storage buffers at set 0, bindings 0..N-1, plus push
// constants — exactly the shape of the app's 30 sim kernels. The transient
// descriptor pool is reset after every submit (legal because submits wait).

#include "rhi/vulkan/vk_common.h"

#include <vk_mem_alloc.h>

namespace krsg::vulkan
{

ResultCode CreateComputePipeline(core::DeviceImpl* impl, core::PipelineRecord& record, const ComputePipelineDesc& desc,
                                 std::uint64_t shaderBackend)
{
    core::VulkanState* state = impl->vk;
    auto* obj = new PipelineObj();
    obj->pushConstantSize = desc.pushConstantSize;
    obj->storageBufferBindings = desc.storageBufferBindings;

    VkDescriptorSetLayoutBinding bindings[16] = {};
    for (std::uint32_t i = 0; i < desc.storageBufferBindings; ++i) {
        bindings[i].binding = i;
        bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptorCount = 1;
        bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    }
    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = desc.storageBufferBindings;
    layoutInfo.pBindings = bindings;
    VkResult vr = vkCreateDescriptorSetLayout(state->device, &layoutInfo, nullptr, &obj->setLayout);
    if (vr != VK_SUCCESS) {
        delete obj;
        return FailVk("vkCreateDescriptorSetLayout", vr);
    }

    VkPushConstantRange pushRange{};
    pushRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushRange.offset = 0;
    pushRange.size = desc.pushConstantSize;

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
    pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &obj->setLayout;
    pipelineLayoutInfo.pushConstantRangeCount = desc.pushConstantSize > 0 ? 1u : 0u;
    pipelineLayoutInfo.pPushConstantRanges = desc.pushConstantSize > 0 ? &pushRange : nullptr;
    vr = vkCreatePipelineLayout(state->device, &pipelineLayoutInfo, nullptr, &obj->layout);
    if (vr != VK_SUCCESS) {
        vkDestroyDescriptorSetLayout(state->device, obj->setLayout, nullptr);
        delete obj;
        return FailVk("vkCreatePipelineLayout", vr);
    }

    VkComputePipelineCreateInfo pipelineInfo{};
    pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    pipelineInfo.stage.module = reinterpret_cast<VkShaderModule>(shaderBackend);
    pipelineInfo.stage.pName = desc.entryPoint;
    pipelineInfo.layout = obj->layout;
    vr = vkCreateComputePipelines(state->device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &obj->pipeline);
    if (vr != VK_SUCCESS) {
        vkDestroyPipelineLayout(state->device, obj->layout, nullptr);
        vkDestroyDescriptorSetLayout(state->device, obj->setLayout, nullptr);
        delete obj;
        return FailVk("vkCreateComputePipelines", vr);
    }

    record.backend = reinterpret_cast<std::uint64_t>(obj);
    return ResultCode::Ok;
}

void DestroyPipeline(core::DeviceImpl* impl, core::PipelineRecord& record)
{
    auto* obj = reinterpret_cast<PipelineObj*>(record.backend);
    if (obj == nullptr) {
        return;
    }
    vkDestroyPipeline(impl->vk->device, obj->pipeline, nullptr);
    vkDestroyPipelineLayout(impl->vk->device, obj->layout, nullptr);
    vkDestroyDescriptorSetLayout(impl->vk->device, obj->setLayout, nullptr);
    delete obj;
    record.backend = 0;
}

ResultCode SubmitCompute(core::DeviceImpl* impl, const core::PipelineRecord& pipeline, const ComputeSubmit& submit,
                         const std::uint64_t* bufferBackends)
{
    core::VulkanState* state = impl->vk;
    auto* obj = reinterpret_cast<PipelineObj*>(pipeline.backend);
    if (obj == nullptr) {
        return ResultCode::Internal;
    }

    // Transient descriptor set for this submit (pool is reset afterwards).
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (obj->storageBufferBindings > 0) {
        VkDescriptorSetAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocInfo.descriptorPool = state->descriptorPool;
        allocInfo.descriptorSetCount = 1;
        allocInfo.pSetLayouts = &obj->setLayout;
        const VkResult ar = vkAllocateDescriptorSets(state->device, &allocInfo, &set);
        if (ar != VK_SUCCESS) {
            return FailVk("vkAllocateDescriptorSets", ar);
        }
        VkDescriptorBufferInfo bufferInfos[16] = {};
        VkWriteDescriptorSet writes[16] = {};
        for (std::uint32_t i = 0; i < submit.bufferCount; ++i) {
            const auto* bufferObj = reinterpret_cast<const BufferObj*>(bufferBackends[i]);
            bufferInfos[i].buffer = bufferObj->buffer;
            bufferInfos[i].offset = 0;
            bufferInfos[i].range = VK_WHOLE_SIZE;
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &bufferInfos[i];
        }
        vkUpdateDescriptorSets(state->device, submit.bufferCount, writes, 0, nullptr);
    }

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VkResult vr = vkBeginCommandBuffer(state->commandBuffer, &beginInfo);
    if (vr != VK_SUCCESS) {
        return FailVk("vkBeginCommandBuffer", vr);
    }

    vkCmdBindPipeline(state->commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, obj->pipeline);
    if (set != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(state->commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, obj->layout, 0, 1, &set, 0,
                                nullptr);
    }
    if (submit.pushConstantSize > 0) {
        vkCmdPushConstants(state->commandBuffer, obj->layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, submit.pushConstantSize,
                           submit.pushConstants);
    }
    vkCmdDispatch(state->commandBuffer, submit.groupsX, submit.groupsY, submit.groupsZ);

    // Make shader writes visible to host reads after the fence (readBuffer).
    VkMemoryBarrier barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(state->commandBuffer, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1,
                         &barrier, 0, nullptr, 0, nullptr);

    vr = vkEndCommandBuffer(state->commandBuffer);
    if (vr != VK_SUCCESS) {
        return FailVk("vkEndCommandBuffer", vr);
    }

    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &state->commandBuffer;
    vr = vkQueueSubmit(state->queue, 1, &submitInfo, state->fence);
    if (vr != VK_SUCCESS) {
        return FailVk("vkQueueSubmit", vr);
    }
    vr = vkWaitForFences(state->device, 1, &state->fence, VK_TRUE, UINT64_MAX);
    vkResetFences(state->device, 1, &state->fence);
    if (set != VK_NULL_HANDLE) {
        vkResetDescriptorPool(state->device, state->descriptorPool, 0);
    }
    if (vr != VK_SUCCESS) {
        return FailVk("vkWaitForFences", vr);
    }
    return ResultCode::Ok;
}

} // namespace krsg::vulkan
