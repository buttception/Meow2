// The implementation of VMA, in its own library so the engine's warning level doesn't apply to it. VMA gets its
// function pointers from volk at runtime (vmaImportVulkanFunctionsFromVolk), so it must not look for them itself.
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
// volk must come first, or VMA leaves out its volk import function.
// clang-format off
#include <volk.h>
#include <vk_mem_alloc.h>
// clang-format on
