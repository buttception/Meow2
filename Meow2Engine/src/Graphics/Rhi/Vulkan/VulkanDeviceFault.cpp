/// @file
/// @brief Device fault reports on Vulkan.

#include "Graphics/Rhi/Vulkan/VulkanDeviceFault.h"

#include <format>

namespace Meow2::Rhi
{
namespace
{
/// @brief What kind of access an address record describes.
/// @param type The type.
/// @return Readable text.
std::string_view addressKind(VkDeviceFaultAddressTypeEXT type)
{
    switch (type)
    {
        case VK_DEVICE_FAULT_ADDRESS_TYPE_READ_INVALID_EXT:
            return "invalid read at";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_WRITE_INVALID_EXT:
            return "invalid write at";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_EXECUTE_INVALID_EXT:
            return "invalid execute at";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_UNKNOWN_EXT:
            return "shader running at";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_INVALID_EXT:
            return "invalid shader instruction at";
        case VK_DEVICE_FAULT_ADDRESS_TYPE_INSTRUCTION_POINTER_FAULT_EXT:
            return "shader faulted at";
        default:
            return "address";
    }
}
} // namespace

std::vector<std::string> describeDeviceFault(std::string_view description,
                                             std::span<const VkDeviceFaultAddressInfoEXT> addresses,
                                             std::span<const VkDeviceFaultVendorInfoEXT> vendor)
{
    std::vector<std::string> lines;
    lines.push_back(std::format("Device fault: {}", description.empty() ? "no description" : description));
    for (const VkDeviceFaultAddressInfoEXT& address : addresses)
    {
        // The precision is a power of two: the fault is somewhere in the block of that size containing the address.
        lines.push_back(std::format("  {} GPU address 0x{:016X} (within 0x{:X} bytes)",
                                    addressKind(address.addressType), address.reportedAddress,
                                    address.addressPrecision));
    }
    for (const VkDeviceFaultVendorInfoEXT& info : vendor)
    {
        lines.push_back(std::format("  vendor: {} (code 0x{:X}, data 0x{:X})", info.description, info.vendorFaultCode,
                                    info.vendorFaultData));
    }
    return lines;
}

std::vector<std::string> readDeviceFault(VkDevice device)
{
    VkDeviceFaultCountsEXT counts{.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_COUNTS_EXT};
    if (vkGetDeviceFaultInfoEXT(device, &counts, nullptr) != VK_SUCCESS)
    {
        return {"Device fault: the driver has no details"};
    }
    std::vector<VkDeviceFaultAddressInfoEXT> addresses(counts.addressInfoCount);
    std::vector<VkDeviceFaultVendorInfoEXT> vendor(counts.vendorInfoCount);
    counts.vendorBinarySize = 0; // the vendor's binary dump is for its own tools; not read
    VkDeviceFaultInfoEXT info{.sType = VK_STRUCTURE_TYPE_DEVICE_FAULT_INFO_EXT,
                              .pAddressInfos = addresses.data(),
                              .pVendorInfos = vendor.data()};
    const VkResult result = vkGetDeviceFaultInfoEXT(device, &counts, &info);
    if (result != VK_SUCCESS && result != VK_INCOMPLETE)
    {
        return {"Device fault: the driver has no details"};
    }
    addresses.resize(counts.addressInfoCount);
    vendor.resize(counts.vendorInfoCount);
    return describeDeviceFault(info.description, addresses, vendor);
}
} // namespace Meow2::Rhi
