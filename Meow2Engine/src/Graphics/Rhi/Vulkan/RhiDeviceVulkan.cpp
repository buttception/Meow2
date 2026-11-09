/// @file
/// @brief The Vulkan device: instance, GPU selection, the queue and the frame loop.

#include "Graphics/Rhi/Vulkan/RhiDeviceVulkan.h"

#include "Graphics/Rhi/Vulkan/RhiSwapchainVulkan.h"
#include "Graphics/Rhi/Vulkan/VulkanDeviceFault.h"
#include "Graphics/Rhi/Vulkan/VulkanRequirements.h"
#include "Meow2/Core/Assert.h"
#include "Meow2/Core/Memory/MemTag.h"
#include "Meow2/Core/Platform.h"

#include <algorithm>
#include <array>
#include <format>
#include <limits>
#include <string>
#include <utility>

namespace Meow2::Rhi
{
namespace
{
constexpr const char* c_validationLayer = "VK_LAYER_KHRONOS_validation";
constexpr std::uint64_t c_frameWaitNanoseconds = 10'000'000'000; // a GPU this late has hung

/// @brief The feature structs Vulkan fills in or reads, chained together. Not copyable: it points into itself.
struct DeviceFeatures
{
    VkPhysicalDeviceMutableDescriptorTypeFeaturesEXT mutableDescriptor{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MUTABLE_DESCRIPTOR_TYPE_FEATURES_EXT};
    VkPhysicalDeviceVulkan13Features v13{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES,
                                         .pNext = &mutableDescriptor};
    VkPhysicalDeviceVulkan12Features v12{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES, .pNext = &v13};
    VkPhysicalDeviceFeatures2 core{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &v12};

    DeviceFeatures() = default;
    DeviceFeatures(const DeviceFeatures&) = delete;
    DeviceFeatures& operator=(const DeviceFeatures&) = delete;
};

/// @brief Reads or sets every descriptor-indexing feature the bindless heap needs, in one place.
/// @param features The features.
/// @param visit    Called with a reference to each field.
template <class Visit>
void forEachBindlessFeature(VkPhysicalDeviceVulkan12Features& features, Visit visit)
{
    visit(features.runtimeDescriptorArray);
    visit(features.descriptorBindingPartiallyBound);
    visit(features.descriptorBindingVariableDescriptorCount);
    visit(features.descriptorBindingUpdateUnusedWhilePending);
    visit(features.descriptorBindingSampledImageUpdateAfterBind);
    visit(features.descriptorBindingStorageImageUpdateAfterBind);
    visit(features.descriptorBindingStorageBufferUpdateAfterBind);
    visit(features.shaderSampledImageArrayNonUniformIndexing);
    visit(features.shaderStorageImageArrayNonUniformIndexing);
    visit(features.shaderStorageBufferArrayNonUniformIndexing);
}

/// @brief Finds the first queue family that does both graphics and compute.
/// @param gpu The GPU.
/// @return The family index, or the maximum value if there's none.
std::uint32_t findQueueFamily(VkPhysicalDevice gpu)
{
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu, &count, families.data());
    const VkQueueFlags wanted = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
    for (std::uint32_t i = 0; i < count; ++i)
    {
        if ((families[i].queueFlags & wanted) == wanted)
        {
            return i;
        }
    }
    return std::numeric_limits<std::uint32_t>::max();
}

/// @brief Collects what a GPU offers for the requirements check.
/// @param gpu        The GPU.
/// @param properties Its properties.
/// @return The facts.
VulkanGpuFacts readGpuFacts(VkPhysicalDevice gpu, const VkPhysicalDeviceProperties& properties)
{
    VulkanGpuFacts facts;
    facts.apiMajor = VK_API_VERSION_MAJOR(properties.apiVersion);
    facts.apiMinor = VK_API_VERSION_MINOR(properties.apiVersion);

    std::uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, extensions.data());
    for (const VkExtensionProperties& extension : extensions)
    {
        facts.extensions.emplace_back(extension.extensionName);
    }

    facts.graphicsAndComputeQueue = findQueueFamily(gpu) != std::numeric_limits<std::uint32_t>::max();
    if (facts.apiMajor == 1 && facts.apiMinor < 3)
    {
        return facts; // the feature structs below don't exist on older drivers
    }
    DeviceFeatures features;
    vkGetPhysicalDeviceFeatures2(gpu, &features.core);
    facts.dynamicRendering = features.v13.dynamicRendering != VK_FALSE;
    facts.synchronization2 = features.v13.synchronization2 != VK_FALSE;
    facts.demoteToHelperInvocation = features.v13.shaderDemoteToHelperInvocation != VK_FALSE;
    facts.textureCompressionBc = features.core.features.textureCompressionBC != VK_FALSE;
    facts.fillModeNonSolid = features.core.features.fillModeNonSolid != VK_FALSE;
    facts.storageImageWithoutFormat = features.core.features.shaderStorageImageWriteWithoutFormat != VK_FALSE;
    facts.timelineSemaphore = features.v12.timelineSemaphore != VK_FALSE;
    facts.bufferDeviceAddress = features.v12.bufferDeviceAddress != VK_FALSE;
    facts.mutableDescriptorType = features.mutableDescriptor.mutableDescriptorType != VK_FALSE;
    facts.multiDrawIndirect = features.core.features.multiDrawIndirect != VK_FALSE &&
                              features.core.features.drawIndirectFirstInstance != VK_FALSE;
    facts.drawIndirectCount = features.v12.drawIndirectCount != VK_FALSE;
    facts.shaderInt64 = features.core.features.shaderInt64 != VK_FALSE;
    facts.bindlessDescriptors = features.core.features.shaderSampledImageArrayDynamicIndexing != VK_FALSE &&
                                features.core.features.shaderStorageBufferArrayDynamicIndexing != VK_FALSE &&
                                features.core.features.shaderStorageImageArrayDynamicIndexing != VK_FALSE &&
                                features.core.features.fragmentStoresAndAtomics != VK_FALSE &&
                                features.core.features.vertexPipelineStoresAndAtomics != VK_FALSE;
    forEachBindlessFeature(features.v12, [&facts](const VkBool32& supported)
                           { facts.bindlessDescriptors = facts.bindlessDescriptors && supported != VK_FALSE; });
    return facts;
}

/// @brief Ranks GPUs that qualify: a dedicated GPU beats a built-in one, and anything beats software.
/// @param type The GPU's type.
/// @return A higher number for a better GPU.
int gpuScore(VkPhysicalDeviceType type)
{
    switch (type)
    {
        case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
            return 4;
        case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
            return 3;
        case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
            return 2;
        default:
            return 1;
    }
}

/// @brief Receives messages from the validation layer, on whichever thread made the call.
VKAPI_ATTR VkBool32 VKAPI_CALL onDebugMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                              VkDebugUtilsMessageTypeFlagsEXT /*types*/,
                                              const VkDebugUtilsMessengerCallbackDataEXT* data, void* userData)
{
    static_cast<RhiDeviceVulkan*>(userData)->onValidationMessage(
        (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0, data->pMessage);
    return VK_FALSE; // never abort the call: the spec reserves that for layer development
}

/// @brief The instance extensions that create window surfaces on this platform.
/// @return Their names.
std::vector<const char*> surfaceExtensions()
{
    std::vector<const char*> names{VK_KHR_SURFACE_EXTENSION_NAME};
#if MEOW2_PLATFORM_WINDOWS
    names.push_back("VK_KHR_win32_surface");
#else
    names.push_back("VK_KHR_xlib_surface");
    names.push_back("VK_KHR_wayland_surface");
#endif
    return names;
}
} // namespace

std::string_view vulkanResultName(VkResult result)
{
    switch (result)
    {
        case VK_SUCCESS:
            return "VK_SUCCESS";
        case VK_TIMEOUT:
            return "VK_TIMEOUT";
        case VK_SUBOPTIMAL_KHR:
            return "VK_SUBOPTIMAL_KHR";
        case VK_ERROR_OUT_OF_HOST_MEMORY:
            return "VK_ERROR_OUT_OF_HOST_MEMORY";
        case VK_ERROR_OUT_OF_DEVICE_MEMORY:
            return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
        case VK_ERROR_INITIALIZATION_FAILED:
            return "VK_ERROR_INITIALIZATION_FAILED";
        case VK_ERROR_DEVICE_LOST:
            return "VK_ERROR_DEVICE_LOST";
        case VK_ERROR_LAYER_NOT_PRESENT:
            return "VK_ERROR_LAYER_NOT_PRESENT";
        case VK_ERROR_EXTENSION_NOT_PRESENT:
            return "VK_ERROR_EXTENSION_NOT_PRESENT";
        case VK_ERROR_FEATURE_NOT_PRESENT:
            return "VK_ERROR_FEATURE_NOT_PRESENT";
        case VK_ERROR_INCOMPATIBLE_DRIVER:
            return "VK_ERROR_INCOMPATIBLE_DRIVER";
        case VK_ERROR_SURFACE_LOST_KHR:
            return "VK_ERROR_SURFACE_LOST_KHR";
        case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR:
            return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
        case VK_ERROR_OUT_OF_DATE_KHR:
            return "VK_ERROR_OUT_OF_DATE_KHR";
        default:
            return "VkResult";
    }
}

RhiDeviceVulkan::RhiDeviceVulkan(Log::Logger& log)
    : m_log(log)
    , m_commandList(*this)
{
}

Result<std::unique_ptr<IRhiDevice>> RhiDeviceVulkan::create(const RhiDeviceDesc& desc, Log::Logger& log)
{
    auto device = std::make_unique<RhiDeviceVulkan>(log);
    if (Result<void> result = device->createInstance(desc); !result)
    {
        return Unexpected<Error>{result.error()};
    }
    if (Result<void> result = device->pickPhysicalDevice(desc); !result)
    {
        return Unexpected<Error>{result.error()};
    }
    if (Result<void> result = device->createDevice(desc); !result)
    {
        return Unexpected<Error>{result.error()};
    }
    if (Result<void> result = device->createBindless(desc); !result)
    {
        return Unexpected<Error>{result.error()};
    }
    return std::unique_ptr<IRhiDevice>{std::move(device)};
}

Result<void> RhiDeviceVulkan::createInstance(const RhiDeviceDesc& desc)
{
    // GLOBAL: volk loads the Vulkan library into process-wide state (techstack.md §18, third-party global state). The
    // library stays loaded until the process ends, so a backend swap needs no reload.
    if (const VkResult result = volkInitialize(); result != VK_SUCCESS)
    {
        return fail(ErrorCode::Unsupported, "Vulkan isn't installed: no graphics driver provides the Vulkan library");
    }
    if (volkGetInstanceVersion() < VK_API_VERSION_1_3)
    {
        return fail(ErrorCode::Unsupported, "The installed Vulkan is older than 1.3; a driver update may help");
    }

    std::uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> available(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, available.data());
    const auto hasExtension = [&available](std::string_view name)
    {
        return std::ranges::any_of(available, [name](const auto& e) { return name == e.extensionName; });
    };

    std::vector<const char*> extensions;
    if (desc.presentation)
    {
        for (const char* name : surfaceExtensions())
        {
            if (hasExtension(name))
            {
                extensions.push_back(name);
            }
        }
    }
    m_debugUtils = hasExtension(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    if (m_debugUtils)
    {
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME); // names in validation messages and capture tools
    }

    std::vector<const char*> layers;
    if (desc.validation)
    {
        vkEnumerateInstanceLayerProperties(&count, nullptr);
        std::vector<VkLayerProperties> installed(count);
        vkEnumerateInstanceLayerProperties(&count, installed.data());
        const bool found = std::ranges::any_of(installed, [](const auto& l)
                                               { return std::string_view{c_validationLayer} == l.layerName; });
        if (found && m_debugUtils)
        {
            layers.push_back(c_validationLayer);
        }
        else
        {
            MEOW2_LOG_WARN(m_log, "Validation was asked for, but {} isn't installed (it comes with the Vulkan SDK)",
                           c_validationLayer);
        }
    }

    const VkApplicationInfo application{.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
                                        .pApplicationName = desc.appName.c_str(),
                                        .pEngineName = "Meow2",
                                        .apiVersion = VK_API_VERSION_1_3};
    const VkInstanceCreateInfo info{.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
                                    .pApplicationInfo = &application,
                                    .enabledLayerCount = static_cast<std::uint32_t>(layers.size()),
                                    .ppEnabledLayerNames = layers.data(),
                                    .enabledExtensionCount = static_cast<std::uint32_t>(extensions.size()),
                                    .ppEnabledExtensionNames = extensions.data()};
    if (const VkResult result = vkCreateInstance(&info, nullptr, &m_instance); result != VK_SUCCESS)
    {
        return fail(ErrorCode::Unsupported, std::format("vkCreateInstance failed: {}", vulkanResultName(result)));
    }
    volkLoadInstance(m_instance);

    if (!layers.empty())
    {
        const VkDebugUtilsMessengerCreateInfoEXT messenger{
            .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
            .messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
            .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT,
            .pfnUserCallback = onDebugMessage,
            .pUserData = this};
        if (const VkResult result = vkCreateDebugUtilsMessengerEXT(m_instance, &messenger, nullptr, &m_messenger);
            result != VK_SUCCESS)
        {
            MEOW2_LOG_WARN(m_log, "No validation messages: {}", vulkanResultName(result));
        }
    }
    return {};
}

Result<void> RhiDeviceVulkan::pickPhysicalDevice(const RhiDeviceDesc& desc)
{
    std::uint32_t count = 0;
    vkEnumeratePhysicalDevices(m_instance, &count, nullptr);
    std::vector<VkPhysicalDevice> gpus(count);
    vkEnumeratePhysicalDevices(m_instance, &count, gpus.data());

    std::string rejected;
    int bestScore = 0;
    for (VkPhysicalDevice gpu : gpus)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(gpu, &properties);
        const std::vector<std::string> missing =
            missingVulkanRequirements(readGpuFacts(gpu, properties), desc.presentation);
        if (!missing.empty())
        {
            rejected += std::format("\n{} is missing:", properties.deviceName);
            for (const std::string& item : missing)
            {
                rejected += std::format("\n  - {}", item);
            }
            continue;
        }
        const int score = gpuScore(properties.deviceType);
        if (score > bestScore)
        {
            bestScore = score;
            m_physicalDevice = gpu;
            m_queueFamily = findQueueFamily(gpu);
            m_adapter = {.name = properties.deviceName,
                         .apiVersion = std::format("{}.{}.{}", VK_API_VERSION_MAJOR(properties.apiVersion),
                                                   VK_API_VERSION_MINOR(properties.apiVersion),
                                                   VK_API_VERSION_PATCH(properties.apiVersion)),
                         .discrete = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU,
                         .software = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_CPU};
        }
    }
    if (m_physicalDevice == VK_NULL_HANDLE)
    {
        return fail(ErrorCode::Unsupported, gpus.empty() ? std::string{"No Vulkan GPU was found"}
                                                         : std::format("No GPU meets the requirements:{}", rejected));
    }
    MEOW2_LOG_INFO(m_log, "GPU: {} (Vulkan {})", m_adapter.name, m_adapter.apiVersion);
    return {};
}

Result<void> RhiDeviceVulkan::createDevice(const RhiDeviceDesc& desc)
{
    MEOW2_ASSERT(desc.recordingThreads > 0, "At least one thread records");
    m_recordingThreads = desc.recordingThreads;
    DeviceFeatures features;
    features.core.features.multiDrawIndirect = VK_TRUE;
    features.core.features.drawIndirectFirstInstance = VK_TRUE; // firstInstance carries the instance index
    features.core.features.shaderInt64 = VK_TRUE;
    features.core.features.samplerAnisotropy = VK_TRUE;
    features.core.features.textureCompressionBC = VK_TRUE; // cooked textures are BC7 and BC5
    features.core.features.fillModeNonSolid = VK_TRUE;     // wireframe debug views
    features.core.features.fragmentStoresAndAtomics = VK_TRUE;
    features.core.features.vertexPipelineStoresAndAtomics = VK_TRUE;
    features.core.features.shaderSampledImageArrayDynamicIndexing = VK_TRUE;
    features.core.features.shaderStorageBufferArrayDynamicIndexing = VK_TRUE;
    features.core.features.shaderStorageImageArrayDynamicIndexing = VK_TRUE;
    // HLSL storage textures carry no format, so SPIR-V declares them "Unknown": this lets shaders write them anyway.
    // (Reading them needs shaderStorageImageReadWithoutFormat; nothing does, so it isn't required.)
    features.core.features.shaderStorageImageWriteWithoutFormat = VK_TRUE;
    features.v12.timelineSemaphore = VK_TRUE;
    features.v12.bufferDeviceAddress = VK_TRUE;
    features.v12.drawIndirectCount = VK_TRUE;
    forEachBindlessFeature(features.v12, [](VkBool32& feature) { feature = VK_TRUE; });
    features.v13.dynamicRendering = VK_TRUE;
    features.v13.synchronization2 = VK_TRUE;
    features.v13.shaderDemoteToHelperInvocation = VK_TRUE; // HLSL discard (alpha-masked materials)
    features.mutableDescriptor.mutableDescriptorType = VK_TRUE;

    std::vector<const char*> extensions;
    for (const std::string_view name : c_requiredVulkanExtensions)
    {
        extensions.push_back(name.data()); // the constants are null-terminated literals
    }
    if (desc.presentation)
    {
        extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    }
    // Device fault reports, where the driver has them: what the GPU was doing when it was lost (techstack.md §8).
    VkPhysicalDeviceFaultFeaturesEXT fault{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT};
    {
        std::uint32_t count = 0;
        vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> available(count);
        vkEnumerateDeviceExtensionProperties(m_physicalDevice, nullptr, &count, available.data());
        const bool hasFault = std::ranges::any_of(
            available, [](const VkExtensionProperties& extension)
            { return std::string_view{extension.extensionName} == VK_EXT_DEVICE_FAULT_EXTENSION_NAME; });
        if (hasFault)
        {
            VkPhysicalDeviceFeatures2 query{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = &fault};
            vkGetPhysicalDeviceFeatures2(m_physicalDevice, &query);
        }
        m_deviceFault = hasFault && fault.deviceFault != VK_FALSE;
    }
    if (m_deviceFault)
    {
        extensions.push_back(VK_EXT_DEVICE_FAULT_EXTENSION_NAME);
        fault = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FAULT_FEATURES_EXT,
                 .pNext = features.core.pNext,
                 .deviceFault = VK_TRUE};
        features.core.pNext = &fault;
    }

    const float priority = 1.0F;
    const VkDeviceQueueCreateInfo queue{.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                        .queueFamilyIndex = m_queueFamily,
                                        .queueCount = 1,
                                        .pQueuePriorities = &priority};
    const VkDeviceCreateInfo info{.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
                                  .pNext = &features.core,
                                  .queueCreateInfoCount = 1,
                                  .pQueueCreateInfos = &queue,
                                  .enabledExtensionCount = static_cast<std::uint32_t>(extensions.size()),
                                  .ppEnabledExtensionNames = extensions.data()};
    if (const VkResult result = vkCreateDevice(m_physicalDevice, &info, nullptr, &m_device); result != VK_SUCCESS)
    {
        return fail(ErrorCode::Unsupported, std::format("vkCreateDevice failed: {}", vulkanResultName(result)));
    }
    volkLoadDevice(m_device);
    vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);
    setObjectName(VK_OBJECT_TYPE_QUEUE, reinterpret_cast<std::uint64_t>(m_queue), "Graphics queue");

    VmaVulkanFunctions functions{};
    VmaAllocatorCreateInfo allocator{};
    allocator.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocator.physicalDevice = m_physicalDevice;
    allocator.device = m_device;
    allocator.instance = m_instance;
    allocator.vulkanApiVersion = VK_API_VERSION_1_3;
    if (const VkResult result = vmaImportVulkanFunctionsFromVolk(&allocator, &functions); result != VK_SUCCESS)
    {
        return fail(ErrorCode::Unknown, std::format("VMA: {}", vulkanResultName(result)));
    }
    allocator.pVulkanFunctions = &functions;
    if (const VkResult result = vmaCreateAllocator(&allocator, &m_allocator); result != VK_SUCCESS)
    {
        return fail(ErrorCode::OutOfMemory, std::format("vmaCreateAllocator failed: {}", vulkanResultName(result)));
    }

    const VkSemaphoreTypeCreateInfo timelineType{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO,
                                                 .semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE,
                                                 .initialValue = 0};
    const VkSemaphoreCreateInfo timeline{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, .pNext = &timelineType};
    if (const VkResult result = vkCreateSemaphore(m_device, &timeline, nullptr, &m_timeline); result != VK_SUCCESS)
    {
        return fail(ErrorCode::Unknown, std::format("Timeline semaphore: {}", vulkanResultName(result)));
    }
    setObjectName(VK_OBJECT_TYPE_SEMAPHORE, reinterpret_cast<std::uint64_t>(m_timeline), "Frame timeline");

    for (Frame& frame : m_frames)
    {
        const VkCommandPoolCreateInfo pool{.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
                                           .queueFamilyIndex = m_queueFamily};
        if (const VkResult result = vkCreateCommandPool(m_device, &pool, nullptr, &frame.pool); result != VK_SUCCESS)
        {
            return fail(ErrorCode::Unknown, std::format("Command pool: {}", vulkanResultName(result)));
        }
        const VkCommandBufferAllocateInfo commands{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                                   .commandPool = frame.pool,
                                                   .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                                   .commandBufferCount = 1};
        if (const VkResult result = vkAllocateCommandBuffers(m_device, &commands, &frame.commands);
            result != VK_SUCCESS)
        {
            return fail(ErrorCode::Unknown, std::format("Command buffer: {}", vulkanResultName(result)));
        }
        // The last list holds the barriers that hand images to the display, after every job's list.
        if (const VkResult result = vkAllocateCommandBuffers(m_device, &commands, &frame.last); result != VK_SUCCESS)
        {
            return fail(ErrorCode::Unknown, std::format("Command buffer: {}", vulkanResultName(result)));
        }
        frame.threads = std::vector<ThreadCommands>(desc.recordingThreads);
        for (ThreadCommands& thread : frame.threads)
        {
            if (const VkResult result = vkCreateCommandPool(m_device, &pool, nullptr, &thread.pool);
                result != VK_SUCCESS)
            {
                return fail(ErrorCode::Unknown, std::format("Command pool: {}", vulkanResultName(result)));
            }
        }
    }
    m_gpuProfiler.connect(m_physicalDevice, m_device, m_queue, m_queueFamily);
    return {};
}

RhiDeviceVulkan::~RhiDeviceVulkan()
{
    MEOW2_ASSERT(m_swapchainCount == 0, "Destroy every swapchain before the device");
    if (m_device != VK_NULL_HANDLE)
    {
        vkDeviceWaitIdle(m_device);
        collectGarbage();
        if (m_defaultSampler != VK_NULL_HANDLE)
        {
            vkDestroySampler(m_device, m_defaultSampler, nullptr);
        }
        vkDestroyPipelineCache(m_device, m_pipelineCache, nullptr);
        vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
        vkDestroyDescriptorPool(m_device, m_heapPool, nullptr);
        vkDestroyDescriptorSetLayout(m_device, m_heapLayout, nullptr);
        m_gpuProfiler.disconnect();
        for (const Frame& frame : m_frames)
        {
            if (frame.pool != VK_NULL_HANDLE)
            {
                vkDestroyCommandPool(m_device, frame.pool, nullptr);
            }
            for (const ThreadCommands& thread : frame.threads)
            {
                if (thread.pool != VK_NULL_HANDLE)
                {
                    vkDestroyCommandPool(m_device, thread.pool, nullptr);
                }
            }
        }
        if (m_timeline != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(m_device, m_timeline, nullptr);
        }
        if (m_allocator != VK_NULL_HANDLE)
        {
            vmaDestroyAllocator(m_allocator);
        }
        vkDestroyDevice(m_device, nullptr);
    }
    if (m_messenger != VK_NULL_HANDLE)
    {
        vkDestroyDebugUtilsMessengerEXT(m_instance, m_messenger, nullptr);
    }
    if (m_instance != VK_NULL_HANDLE)
    {
        vkDestroyInstance(m_instance, nullptr);
    }
}

Result<std::unique_ptr<IRhiSwapchain>> RhiDeviceVulkan::createSwapchain(const RhiSwapchainDesc& desc)
{
    return RhiSwapchainVulkan::create(*this, desc);
}

Result<IRhiCommandList*> RhiDeviceVulkan::beginFrame()
{
    MEOW2_ASSERT(!m_inFrame, "endFrame() wasn't called");
    Frame& frame = m_frames[m_frameIndex];
    const VkSemaphoreWaitInfo wait{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO,
                                   .semaphoreCount = 1,
                                   .pSemaphores = &m_timeline,
                                   .pValues = &frame.submittedValue};
    if (m_lost)
    {
        return Unexpected<Error>{reportDeviceLoss()};
    }
    if (const VkResult result = vkWaitSemaphores(m_device, &wait, c_frameWaitNanoseconds); result != VK_SUCCESS)
    {
        if (result == VK_ERROR_DEVICE_LOST)
        {
            return Unexpected<Error>{reportDeviceLoss()};
        }
        return fail(result == VK_TIMEOUT ? ErrorCode::Timeout : ErrorCode::Unknown,
                    std::format("Waiting for the GPU: {}", vulkanResultName(result)));
    }
    collectGarbage();
    vkResetCommandPool(m_device, frame.pool, 0);
    for (ThreadCommands& thread : frame.threads)
    {
        vkResetCommandPool(m_device, thread.pool, 0);
        thread.used = 0;
    }
    if (Result<void> begun = beginCommands(frame.commands); !begun)
    {
        return Unexpected<Error>{begun.error()};
    }
    if (Result<void> begun = beginCommands(frame.last); !begun)
    {
        return Unexpected<Error>{begun.error()};
    }
    m_gpuProfiler.collect(frame.commands);
    m_commandList.setCommandBuffer(frame.commands);
    m_inFrame = true;
    return &m_commandList;
}

Result<void> RhiDeviceVulkan::beginCommands(VkCommandBuffer commands)
{
    const VkCommandBufferBeginInfo begin{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
                                         .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT};
    if (const VkResult result = vkBeginCommandBuffer(commands, &begin); result != VK_SUCCESS)
    {
        return fail(ErrorCode::Unknown, std::format("vkBeginCommandBuffer: {}", vulkanResultName(result)));
    }
    // Bound once per list and never again: nothing else in the engine is ever bound (techstack.md §11).
    for (const VkPipelineBindPoint point : {VK_PIPELINE_BIND_POINT_GRAPHICS, VK_PIPELINE_BIND_POINT_COMPUTE})
    {
        vkCmdBindDescriptorSets(commands, point, m_pipelineLayout, 0, 1, &m_heapSet, 0, nullptr);
    }
    return {};
}

Result<IRhiCommandList*> RhiDeviceVulkan::commandList(std::uint32_t threadIndex)
{
    MEOW2_ASSERT(m_inFrame, "commandList() outside a frame");
    MEOW2_ASSERT(threadIndex < m_recordingThreads, "RhiDeviceDesc::recordingThreads is too small");
    const Memory::MemTagScope tag{Memory::MemTag::Rhi};
    ThreadCommands& thread = m_frames[m_frameIndex].threads[threadIndex];
    if (thread.used == thread.buffers.size())
    {
        const VkCommandBufferAllocateInfo info{.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                                               .commandPool = thread.pool,
                                               .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                                               .commandBufferCount = 1};
        VkCommandBuffer buffer = VK_NULL_HANDLE;
        if (const VkResult result = vkAllocateCommandBuffers(m_device, &info, &buffer); result != VK_SUCCESS)
        {
            return fail(ErrorCode::Unknown, std::format("Command buffer: {}", vulkanResultName(result)));
        }
        thread.buffers.push_back(buffer);
        thread.lists.emplace_back(*this);
    }
    if (Result<void> begun = beginCommands(thread.buffers[thread.used]); !begun)
    {
        return Unexpected<Error>{begun.error()};
    }
    RhiCommandListVulkan& list = thread.lists[thread.used];
    list.setCommandBuffer(thread.buffers[thread.used]);
    ++thread.used;
    return &list;
}

void RhiDeviceVulkan::recordSwapchainBarrier(VkCommandBuffer commands, const RhiSwapchainVulkan& swapchain,
                                             bool toPresent)
{
    VkImageMemoryBarrier2 barrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = swapchain.image(),
        .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .levelCount = 1, .layerCount = 1}};
    if (toPresent)
    {
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    }
    else
    {
        // UNDEFINED: the old contents don't matter. The stage matches the submit's wait on the acquire semaphore, so
        // the change happens after the display has let go of the image.
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    const VkDependencyInfo dependency{
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier};
    vkCmdPipelineBarrier2(commands, &dependency);
}

bool RhiDeviceVulkan::acquire(IRhiSwapchain& swapchain)
{
    MEOW2_ASSERT(m_inFrame, "acquire() outside a frame");
    auto& target = static_cast<RhiSwapchainVulkan&>(swapchain);
    if (!target.acquire(m_frameIndex))
    {
        return false;
    }
    recordSwapchainBarrier(m_frames[m_frameIndex].commands, target, false);
    m_acquired.push_back(&target);
    return true;
}

Result<void> RhiDeviceVulkan::endFrame(std::span<IRhiCommandList* const> lists)
{
    MEOW2_ASSERT(m_inFrame, "beginFrame() wasn't called");
    m_inFrame = false;
    Frame& frame = m_frames[m_frameIndex];

    std::vector<VkSemaphoreSubmitInfo> waits;
    std::vector<VkSemaphoreSubmitInfo> signals;
    std::vector<VkSemaphore> presentWaits;
    std::vector<VkSwapchainKHR> swapchains;
    std::vector<std::uint32_t> imageIndices;
    for (const RhiSwapchainVulkan* swapchain : m_acquired)
    {
        recordSwapchainBarrier(frame.last, *swapchain, true);
        waits.push_back({.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                         .semaphore = swapchain->acquireSemaphore(m_frameIndex),
                         .stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT});
        signals.push_back({.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                           .semaphore = swapchain->presentSemaphore(),
                           .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT});
        presentWaits.push_back(swapchain->presentSemaphore());
        swapchains.push_back(swapchain->handle());
        imageIndices.push_back(swapchain->imageIndex());
    }
    MEOW2_ASSERT(!m_commandList.isOpen(), "The frame's list has rendering or a marker still open");
    std::vector<VkCommandBuffer> buffers{frame.commands};
    for (IRhiCommandList* list : lists)
    {
        const auto* recorded = static_cast<const RhiCommandListVulkan*>(list);
        MEOW2_ASSERT(!recorded->isOpen(), "A list has rendering or a marker still open");
        buffers.push_back(recorded->commandBuffer());
    }
    buffers.push_back(frame.last);
    [[maybe_unused]] std::size_t recordedLists = 0;
    for (const ThreadCommands& thread : frame.threads)
    {
        recordedLists += thread.used;
    }
    MEOW2_ASSERT(recordedLists == lists.size(), "endFrame() must get every list from commandList()");
    std::vector<VkCommandBufferSubmitInfo> commands;
    commands.reserve(buffers.size());
    for (const VkCommandBuffer buffer : buffers)
    {
        if (const VkResult result = vkEndCommandBuffer(buffer); result != VK_SUCCESS)
        {
            return fail(ErrorCode::Unknown, std::format("vkEndCommandBuffer: {}", vulkanResultName(result)));
        }
        commands.push_back({.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO, .commandBuffer = buffer});
    }

    frame.submittedValue = ++m_timelineValue;
    signals.push_back({.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO,
                       .semaphore = m_timeline,
                       .value = frame.submittedValue,
                       .stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT});
    const VkSubmitInfo2 submit{.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2,
                               .waitSemaphoreInfoCount = static_cast<std::uint32_t>(waits.size()),
                               .pWaitSemaphoreInfos = waits.data(),
                               .commandBufferInfoCount = static_cast<std::uint32_t>(commands.size()),
                               .pCommandBufferInfos = commands.data(),
                               .signalSemaphoreInfoCount = static_cast<std::uint32_t>(signals.size()),
                               .pSignalSemaphoreInfos = signals.data()};
    const VkResult submitted = vkQueueSubmit2(m_queue, 1, &submit, VK_NULL_HANDLE);
    m_frameIndex = (m_frameIndex + 1) % c_maxFramesInFlight;
    if (submitted != VK_SUCCESS)
    {
        for (RhiSwapchainVulkan* swapchain : m_acquired)
        {
            swapchain->released();
        }
        m_acquired.clear();
        if (submitted == VK_ERROR_DEVICE_LOST)
        {
            return Unexpected<Error>{reportDeviceLoss()};
        }
        return fail(ErrorCode::Unknown, std::format("vkQueueSubmit2: {}", vulkanResultName(submitted)));
    }

    Result<void> outcome;
    if (!swapchains.empty())
    {
        std::vector<VkResult> results(swapchains.size(), VK_SUCCESS);
        const VkPresentInfoKHR present{.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
                                       .waitSemaphoreCount = static_cast<std::uint32_t>(presentWaits.size()),
                                       .pWaitSemaphores = presentWaits.data(),
                                       .swapchainCount = static_cast<std::uint32_t>(swapchains.size()),
                                       .pSwapchains = swapchains.data(),
                                       .pImageIndices = imageIndices.data(),
                                       .pResults = results.data()};
        const VkResult presented = vkQueuePresentKHR(m_queue, &present);
        for (std::size_t i = 0; i < results.size(); ++i)
        {
            if (results[i] == VK_SUBOPTIMAL_KHR || results[i] == VK_ERROR_OUT_OF_DATE_KHR)
            {
                m_acquired[i]->markOutOfDate();
            }
        }
        if (presented == VK_ERROR_DEVICE_LOST)
        {
            outcome = Unexpected<Error>{reportDeviceLoss()};
        }
    }
    for (RhiSwapchainVulkan* swapchain : m_acquired)
    {
        swapchain->released();
    }
    m_acquired.clear();
    return outcome;
}

void RhiDeviceVulkan::waitIdle()
{
    vkDeviceWaitIdle(m_device);
    collectGarbage();
}

void RhiDeviceVulkan::setObjectName(VkObjectType type, std::uint64_t handle, const char* name) const
{
    if (!m_debugUtils)
    {
        return;
    }
    const VkDebugUtilsObjectNameInfoEXT info{.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
                                             .objectType = type,
                                             .objectHandle = handle,
                                             .pObjectName = name};
    vkSetDebugUtilsObjectNameEXT(m_device, &info);
}

Error RhiDeviceVulkan::reportDeviceLoss()
{
    Error error{.code = ErrorCode::Unknown, .message = "The GPU device was lost (VK_ERROR_DEVICE_LOST)"};
    if (m_lost.exchange(true))
    {
        return error; // reported once; every later frame just fails
    }
    MEOW2_LOG_ERROR(m_log, "{}", error.message);
    if (!m_deviceFault)
    {
        MEOW2_LOG_ERROR(m_log, "No details: this driver doesn't have VK_EXT_device_fault");
        return error;
    }
    for (const std::string& line : readDeviceFault(m_device))
    {
        MEOW2_LOG_ERROR(m_log, "{}", line);
    }
    return error;
}

void RhiDeviceVulkan::onValidationMessage(bool isError, const char* message)
{
    if (isError)
    {
        ++m_validationErrors;
        MEOW2_LOG_ERROR(m_log, "Vulkan: {}", message);
    }
    else
    {
        MEOW2_LOG_WARN(m_log, "Vulkan: {}", message);
    }
}
} // namespace Meow2::Rhi
