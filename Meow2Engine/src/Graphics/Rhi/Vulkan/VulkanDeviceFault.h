/// @file
/// @brief What `VK_EXT_device_fault` says about a lost device, as log lines.

#pragma once

#include <volk.h>

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace Meow2::Rhi
{
/// @brief Turns fault information into readable lines.
/// @param description The driver's description of the fault.
/// @param addresses   GPU addresses involved: what was read, written or executed.
/// @param vendor      The vendor's own fault codes.
/// @return One line for the fault, then one per address and vendor record.
[[nodiscard]] std::vector<std::string> describeDeviceFault(std::string_view description,
                                                           std::span<const VkDeviceFaultAddressInfoEXT> addresses,
                                                           std::span<const VkDeviceFaultVendorInfoEXT> vendor);

/// @brief Asks the driver what went wrong after VK_ERROR_DEVICE_LOST.
/// @param device A device with `VK_EXT_device_fault` enabled.
/// @return The description from describeDeviceFault(), or one line saying the driver had nothing to say.
[[nodiscard]] std::vector<std::string> readDeviceFault(VkDevice device);
} // namespace Meow2::Rhi
