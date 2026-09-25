// Minimal real Vulkan backend (WP0 scope): instance + physical-device selection +
// logical device + caps reporting, with validation layers when requested and
// portability handling for MoltenVK. WP1 replaces the raw loader link with volk,
// adds VMA, queues/frames, and real resource objects.

#include <cstring>
#include <vector>

#include <vulkan/vulkan.h>

#include "core/device_impl.h"

namespace krsg::vulkan
{

struct State {
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physical = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    std::uint32_t queueFamily = 0;
};

} // namespace krsg::vulkan

namespace krsg::core
{
struct VulkanState : krsg::vulkan::State {
};
} // namespace krsg::core

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

} // namespace

ResultCode CreateVulkanDevice(const DeviceDesc& desc, core::DeviceImpl* impl)
{
    auto* state = new core::VulkanState();

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = desc.applicationName;
    appInfo.pEngineName = "KRSGraphics";
    appInfo.engineVersion = VK_MAKE_VERSION(KRSG_VERSION_MAJOR, KRSG_VERSION_MINOR, KRSG_VERSION_PATCH);
    appInfo.apiVersion = VK_API_VERSION_1_2;

    std::vector<const char*> layers;
    if (desc.enableValidation && hasLayer("VK_LAYER_KHRONOS_validation")) {
        layers.push_back("VK_LAYER_KHRONOS_validation");
    }

    std::vector<const char*> extensions;
    VkInstanceCreateFlags flags = 0;
#if defined(VK_KHR_portability_enumeration)
    if (hasInstanceExtension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME)) {
        extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    }
#endif

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.flags = flags;
    instanceInfo.pApplicationInfo = &appInfo;
    instanceInfo.enabledLayerCount = static_cast<std::uint32_t>(layers.size());
    instanceInfo.ppEnabledLayerNames = layers.data();
    instanceInfo.enabledExtensionCount = static_cast<std::uint32_t>(extensions.size());
    instanceInfo.ppEnabledExtensionNames = extensions.data();

    if (vkCreateInstance(&instanceInfo, nullptr, &state->instance) != VK_SUCCESS) {
        core::EmitDebug(Severity::Error, "krsg: vkCreateInstance failed (no loader/ICD?)");
        delete state;
        return ResultCode::Unsupported;
    }

    std::uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(state->instance, &deviceCount, nullptr);
    if (deviceCount == 0) {
        core::EmitDebug(Severity::Error, "krsg: no Vulkan physical devices enumerated");
        vkDestroyInstance(state->instance, nullptr);
        delete state;
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
        core::EmitDebug(Severity::Error, "krsg: no graphics+compute queue family");
        vkDestroyInstance(state->instance, nullptr);
        delete state;
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

    if (vkCreateDevice(state->physical, &deviceInfo, nullptr, &state->device) != VK_SUCCESS) {
        core::EmitDebug(Severity::Error, "krsg: vkCreateDevice failed");
        vkDestroyInstance(state->instance, nullptr);
        delete state;
        return ResultCode::Unsupported;
    }

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(state->physical, &props);
    impl->capsData.backend = BackendKind::Vulkan;
    std::strncpy(impl->capsData.deviceName, props.deviceName, sizeof(impl->capsData.deviceName) - 1);
    impl->capsData.apiMajor = VK_API_VERSION_MAJOR(props.apiVersion);
    impl->capsData.apiMinor = VK_API_VERSION_MINOR(props.apiVersion);
    impl->capsData.softwareRasterizer = (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU);
    impl->capsData.portabilitySubset = portability;
    impl->vk = state;
    return ResultCode::Ok;
}

void DestroyVulkanDevice(core::DeviceImpl* impl)
{
    auto* state = impl->vk;
    if (state == nullptr) {
        return;
    }
    if (state->device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(state->device);
        vkDestroyDevice(state->device, nullptr);
    }
    if (state->instance != VK_NULL_HANDLE) {
        vkDestroyInstance(state->instance, nullptr);
    }
    delete state;
    impl->vk = nullptr;
}

} // namespace krsg::vulkan
