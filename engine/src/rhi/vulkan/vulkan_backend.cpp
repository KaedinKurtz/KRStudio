// Vulkan backend: device lifetime. volk resolves the loader at runtime (no
// link-time Vulkan dependency); VMA owns memory; a debug-utils messenger
// forwards validation-layer findings into the krsg debug sink, which is how
// CI's "any validation message is a failure" rule is enforced structurally.

#include <cstdio>
#include <cstring>
#include <vector>

#include "rhi/vulkan/vk_common.h"

#include <vk_mem_alloc.h>

namespace krsg::vulkan
{

namespace
{

bool hasLayer(const char* name)
{
    std::uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const auto& layer : layers) {
        if (std::strcmp(layer.layerName, name) == 0) {
            return true;
        }
    }
    return false;
}

bool hasInstanceExtension(const char* name)
{
    std::uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, exts.data());
    for (const auto& ext : exts) {
        if (std::strcmp(ext.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

bool hasDeviceExtension(VkPhysicalDevice physical, const char* name)
{
    std::uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, exts.data());
    for (const auto& ext : exts) {
        if (std::strcmp(ext.extensionName, name) == 0) {
            return true;
        }
    }
    return false;
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugMessengerThunk(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                                   VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                                   const VkDebugUtilsMessengerCallbackDataEXT* data, void* /*user*/)
{
    krsg::Severity mapped = krsg::Severity::Info;
    if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0) {
        mapped = krsg::Severity::Error;
    } else if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) != 0) {
        mapped = krsg::Severity::Warning;
    }
    char line[1024];
    std::snprintf(line, sizeof(line), "vulkan-validation: %s",
                  data != nullptr && data->pMessage != nullptr ? data->pMessage : "(no message)");
    core::EmitDebug(mapped, line);
    return VK_FALSE;
}

void destroyState(core::VulkanState* state)
{
    if (state->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(state->device);
    }
    if (state->descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(state->device, state->descriptorPool, nullptr);
    }
    if (state->fence != VK_NULL_HANDLE) {
        vkDestroyFence(state->device, state->fence, nullptr);
    }
    if (state->commandPool != VK_NULL_HANDLE) {
        vkDestroyCommandPool(state->device, state->commandPool, nullptr);
    }
    if (state->allocator != nullptr) {
        vmaDestroyAllocator(state->allocator);
    }
    if (state->device != VK_NULL_HANDLE) {
        vkDestroyDevice(state->device, nullptr);
    }
    if (state->messenger != VK_NULL_HANDLE && vkDestroyDebugUtilsMessengerEXT != nullptr) {
        vkDestroyDebugUtilsMessengerEXT(state->instance, state->messenger, nullptr);
    }
    if (state->instance != VK_NULL_HANDLE) {
        vkDestroyInstance(state->instance, nullptr);
    }
    delete state;
}

} // namespace

ResultCode FailVk(const char* what, VkResult result)
{
    char line[256];
    std::snprintf(line, sizeof(line), "krsg-vulkan: %s failed (VkResult %d)", what, static_cast<int>(result));
    core::EmitDebug(Severity::Error, line);
    switch (result) {
    case VK_ERROR_OUT_OF_HOST_MEMORY:
    case VK_ERROR_OUT_OF_DEVICE_MEMORY:
        return ResultCode::OutOfMemory;
    case VK_ERROR_DEVICE_LOST:
        return ResultCode::DeviceLost;
    default:
        return ResultCode::Internal;
    }
}

ResultCode CreateVulkanDevice(const DeviceDesc& desc, core::DeviceImpl* impl)
{
    if (volkInitialize() != VK_SUCCESS) {
        core::EmitDebug(Severity::Error, "krsg-vulkan: no Vulkan loader on this system (volkInitialize failed)");
        return ResultCode::Unsupported;
    }

    auto* state = new core::VulkanState();

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = desc.applicationName;
    appInfo.pEngineName = "KRSGraphics";
    appInfo.engineVersion = VK_MAKE_VERSION(KRSG_VERSION_MAJOR, KRSG_VERSION_MINOR, KRSG_VERSION_PATCH);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    std::vector<const char*> layers;
    const bool validation = desc.enableValidation && hasLayer("VK_LAYER_KHRONOS_validation");
    if (validation) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }

    std::vector<const char*> extensions;
    VkInstanceCreateFlags flags = 0;
    if (hasInstanceExtension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
    const bool debugUtils = validation && hasInstanceExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (debugUtils) {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.flags = flags;
    instanceInfo.pApplicationInfo = &appInfo;
    instanceInfo.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    instanceInfo.ppEnabledLayerNames = layers.data();
    instanceInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    instanceInfo.ppEnabledExtensionNames = extensions.data();

    VkResult vr = vkCreateInstance(&instanceInfo, nullptr, &state->instance);
    if (vr != VK_SUCCESS) {
        core::EmitDebug(Severity::Error, "krsg-vulkan: vkCreateInstance failed (no ICD?)");
        delete state;
        return ResultCode::Unsupported;
    }
    volkLoadInstance(state->instance);

    if (debugUtils && vkCreateDebugUtilsMessengerEXT != nullptr) {
        VkDebugUtilsMessengerCreateInfoEXT messengerInfo{};
        messengerInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        messengerInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                                    VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        messengerInfo.pfnUserCallback = &debugMessengerThunk;
        vkCreateDebugUtilsMessengerEXT(state->instance, &messengerInfo, nullptr, &state->messenger);
    }

    std::uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(state->instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        core::EmitDebug(Severity::Error, "krsg-vulkan: no physical devices enumerated");
        destroyState(state);
        return ResultCode::Unsupported;
    }
    std::vector<VkPhysicalDevice> physicals(deviceCount);
    vkEnumeratePhysicalDevices(state->instance, &deviceCount, physicals.data());

    // Prefer a discrete GPU; otherwise take the first device (lavapipe in CI).
    state->physical = physicals[0];
    for (VkPhysicalDevice candidate : physicals) {
        VkPhysicalDeviceProperties props;
        vkGetPhysicalDeviceProperties(candidate, &props);
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            state->physical = candidate;
            break;
        }
    }

    std::uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(state->physical, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(state->physical, &familyCount, families.data());
    bool familyFound = false;
    for (std::uint32_t i = 0; i < familyCount; ++i) {
        const VkQueueFlags needed = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
        if ((families[i].queueFlags & needed) == needed) {
            state->queueFamily = i;
            familyFound = true;
            break;
        }
    }
    if (!familyFound) {
        core::EmitDebug(Severity::Error, "krsg-vulkan: no graphics+compute queue family");
        destroyState(state);
        return ResultCode::Unsupported;
    }

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = state->queueFamily;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    // MoltenVK: VK_KHR_portability_subset MUST be enabled when the device offers it.
    std::vector<const char*> deviceExtensions;
    bool portability = false;
    if (hasDeviceExtension(state->physical, "VK_KHR_portability_subset")) {
        deviceExtensions.push_back("VK_KHR_portability_subset");
        portability = true;
    }

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = static_cast<std::uint32_t>(deviceExtensions.size());
    deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();

    vr = vkCreateDevice(state->physical, &deviceInfo, nullptr, &state->device);
    if (vr != VK_SUCCESS) {
        destroyState(state);
        return FailVk("vkCreateDevice", vr);
    }
    volkLoadDevice(state->device);
    vkGetDeviceQueue(state->device, state->queueFamily, 0, &state->queue);

    // VMA with volk-resolved entry points.
    VmaVulkanFunctions vmaFunctions{};
    vmaFunctions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    vmaFunctions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;
    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = state->physical;
    allocatorInfo.device = state->device;
    allocatorInfo.instance = state->instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_2;
    allocatorInfo.pVulkanFunctions = &vmaFunctions;
    vr = vmaCreateAllocator(&allocatorInfo, &state->allocator);
    if (vr != VK_SUCCESS) {
        destroyState(state);
        return FailVk("vmaCreateAllocator", vr);
    }

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = state->queueFamily;
    vr = vkCreateCommandPool(state->device, &poolInfo, nullptr, &state->commandPool);
    if (vr != VK_SUCCESS) {
        destroyState(state);
        return FailVk("vkCreateCommandPool", vr);
    }

    VkCommandBufferAllocateInfo cmdInfo{};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmdInfo.commandPool = state->commandPool;
    cmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmdInfo.commandBufferCount = 1;
    vr = vkAllocateCommandBuffers(state->device, &cmdInfo, &state->commandBuffer);
    if (vr != VK_SUCCESS) {
        destroyState(state);
        return FailVk("vkAllocateCommandBuffers", vr);
    }

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    vr = vkCreateFence(state->device, &fenceInfo, nullptr, &state->fence);
    if (vr != VK_SUCCESS) {
        destroyState(state);
        return FailVk("vkCreateFence", vr);
    }

    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSize.descriptorCount = 256;
    VkDescriptorPoolCreateInfo descPoolInfo{};
    descPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    descPoolInfo.maxSets = 64;
    descPoolInfo.poolSizeCount = 1;
    descPoolInfo.pPoolSizes = &poolSize;
    vr = vkCreateDescriptorPool(state->device, &descPoolInfo, nullptr, &state->descriptorPool);
    if (vr != VK_SUCCESS) {
        destroyState(state);
        return FailVk("vkCreateDescriptorPool", vr);
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(state->physical, &props);
    impl->capsData.backend = BackendKind::Vulkan;
    core::CopyName(impl->capsData.deviceName, sizeof(impl->capsData.deviceName), props.deviceName);
    impl->capsData.apiMajor = VK_API_VERSION_MAJOR(props.apiVersion);
    impl->capsData.apiMinor = VK_API_VERSION_MINOR(props.apiVersion);
    impl->capsData.softwareRasterizer = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU);
    impl->capsData.portabilitySubset = portability;
    impl->vk = state;
    return ResultCode::Ok;
}

void DestroyVulkanDevice(core::DeviceImpl* impl)
{
    if (impl->vk == nullptr) {
        return;
    }
    destroyState(impl->vk);
    impl->vk = nullptr;
}

} // namespace krsg::vulkan
