/// @file
/// @brief createRhiDevice(): picks the backend at runtime.

#include "Meow2/Graphics/Rhi/IRhiDevice.h"

#if MEOW2_RHI_VULKAN
#include "Graphics/Rhi/Vulkan/RhiDeviceVulkan.h"
#endif
#if MEOW2_RHI_D3D12
#include "Graphics/Rhi/D3D12/RhiDeviceD3D12.h"
#endif

namespace Meow2::Rhi
{
Result<std::unique_ptr<IRhiDevice>> createRhiDevice(const RhiDeviceDesc& desc, [[maybe_unused]] Log::Logger& log)
{
    switch (desc.backend)
    {
        case RhiBackend::Vulkan:
#if MEOW2_RHI_VULKAN
            return RhiDeviceVulkan::create(desc, log);
#else
            return fail(ErrorCode::Unsupported, "This build has no Vulkan backend (MEOW2_RHI_VULKAN=OFF)");
#endif
        case RhiBackend::D3D12:
#if MEOW2_RHI_D3D12
            return RhiDeviceD3D12::create(desc, log);
#else
            return fail(ErrorCode::Unsupported,
                        "This build has no D3D12 backend (MEOW2_RHI_D3D12=OFF, or not Windows)");
#endif
    }
    return fail(ErrorCode::Unsupported, "Unknown backend");
}
} // namespace Meow2::Rhi
