/// @file
/// @brief RHI tests that need no GPU: handles, the requirements check, backend selection.

#include "Graphics/Rhi/Vulkan/VulkanRequirements.h"

#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Graphics/Rhi/IRhiDevice.h>
#include <Meow2/Graphics/Rhi/RhiHandle.h>

#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace Meow2::Rhi
{
namespace
{
struct TestTag
{
};
using TestHandle = Handle<TestTag>;
using TestPool = HandlePool<int, TestTag>;

/// @brief A GPU that has everything.
VulkanGpuFacts capableGpu()
{
    VulkanGpuFacts facts;
    facts.apiMajor = 1;
    facts.apiMinor = 3;
    facts.extensions = {"VK_EXT_mutable_descriptor_type", "VK_KHR_swapchain"};
    facts.graphicsAndComputeQueue = true;
    facts.dynamicRendering = true;
    facts.synchronization2 = true;
    facts.timelineSemaphore = true;
    facts.bufferDeviceAddress = true;
    facts.bindlessDescriptors = true;
    facts.mutableDescriptorType = true;
    facts.multiDrawIndirect = true;
    facts.drawIndirectCount = true;
    facts.shaderInt64 = true;
    return facts;
}

bool mentions(const std::vector<std::string>& lines, const std::string& text)
{
    return std::ranges::any_of(lines,
                               [&text](const std::string& line) { return line.find(text) != std::string::npos; });
}
} // namespace

TEST(Handle, DefaultIsNotSet)
{
    EXPECT_FALSE(TestHandle{}.isSet());
    TestPool pool;
    EXPECT_EQ(pool.get(TestHandle{}), nullptr);
}

TEST(Handle, CreateAndGet)
{
    TestPool pool;
    const TestHandle handle = pool.create(42);
    ASSERT_TRUE(handle.isSet());
    ASSERT_NE(pool.get(handle), nullptr);
    EXPECT_EQ(*pool.get(handle), 42);
    EXPECT_EQ(pool.size(), 1U);
}

TEST(Handle, DestroyedHandleIsStale)
{
    TestPool pool;
    const TestHandle handle = pool.create(1);
    EXPECT_TRUE(pool.destroy(handle));
    EXPECT_EQ(pool.get(handle), nullptr);
    EXPECT_FALSE(pool.destroy(handle));
    EXPECT_EQ(pool.size(), 0U);
}

TEST(Handle, ReusedSlotDoesNotAnswerTheOldHandle)
{
    TestPool pool;
    const TestHandle first = pool.create(1);
    pool.destroy(first);
    const TestHandle second = pool.create(2);
    EXPECT_EQ(second.index, first.index);
    EXPECT_NE(second.generation, first.generation);
    EXPECT_EQ(pool.get(first), nullptr);
    EXPECT_EQ(*pool.get(second), 2);
}

TEST(Requirements, CapableGpuPasses)
{
    EXPECT_TRUE(missingVulkanRequirements(capableGpu(), true).empty());
}

TEST(Requirements, OldVulkanIsNamedWithItsVersion)
{
    VulkanGpuFacts facts = capableGpu();
    facts.apiMinor = 2;
    EXPECT_TRUE(mentions(missingVulkanRequirements(facts, false), "it has 1.2"));
}

TEST(Requirements, MissingExtensionIsNamed)
{
    VulkanGpuFacts facts = capableGpu();
    facts.extensions = {"VK_KHR_swapchain"};
    EXPECT_TRUE(mentions(missingVulkanRequirements(facts, true), "VK_EXT_mutable_descriptor_type"));
}

TEST(Requirements, MissingFeatureIsNamed)
{
    VulkanGpuFacts facts = capableGpu();
    facts.bindlessDescriptors = false;
    EXPECT_TRUE(mentions(missingVulkanRequirements(facts, true), "bindless"));
}

TEST(Requirements, SwapchainOnlyNeededForPresentation)
{
    VulkanGpuFacts facts = capableGpu();
    facts.extensions = {"VK_EXT_mutable_descriptor_type"};
    EXPECT_TRUE(missingVulkanRequirements(facts, false).empty());
    EXPECT_TRUE(mentions(missingVulkanRequirements(facts, true), "VK_KHR_swapchain"));
}

TEST(Device, D3D12NotBuiltIsAnError)
{
    Log::LogSystem logSystem{{.appName = "Test", .directory = {}, .console = false}};
    Log::Logger log = logSystem.createLogger("RHI");
    const Result<std::unique_ptr<IRhiDevice>> device = createRhiDevice({.backend = RhiBackend::D3D12}, log);
    ASSERT_FALSE(device);
    EXPECT_EQ(device.error().code, ErrorCode::Unsupported);
}
} // namespace Meow2::Rhi
