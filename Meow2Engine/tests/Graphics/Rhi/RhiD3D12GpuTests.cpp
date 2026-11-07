/// @file
/// @brief D3D12-only tests on a real GPU (`MEOW2_GPU_TESTS=ON`): the Agility runtime is the one that's loaded, and a
///        lost device is reported with DRED.

#include "Graphics/Rhi/D3D12/RhiDeviceD3D12.h"

#include <Meow2/Core/Log/LogSystem.h>
#include <Meow2/Graphics/Rhi/IRhiDevice.h>

#include <gtest/gtest.h>
#include <windows.h>

#include <array>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <system_error>

namespace Meow2::Rhi
{
namespace fs = std::filesystem;

TEST(AgilityTest, UsesTheAgilityRuntime)
{
    Log::LogSystem logSystem{{.appName = "Test", .directory = {}, .console = true}};
    Log::Logger log = logSystem.createLogger("RHI");
    Result<std::unique_ptr<IRhiDevice>> device =
        createRhiDevice({.backend = RhiBackend::D3D12, .appName = "Meow2 tests", .presentation = false}, log);
    ASSERT_TRUE(device) << device.error().message;

    // Windows loads D3D12Core.dll when the device is created: from D3D12/ next to the exe if the exports were found,
    // else from System32.
    const HMODULE core = GetModuleHandleW(L"D3D12Core.dll");
    ASSERT_NE(core, nullptr);
    std::array<wchar_t, 1024> path{};
    GetModuleFileNameW(core, path.data(), static_cast<DWORD>(path.size()));
    std::array<wchar_t, 1024> exe{};
    GetModuleFileNameW(nullptr, exe.data(), static_cast<DWORD>(exe.size()));
    EXPECT_EQ(fs::path{path.data()}.parent_path(), fs::path{exe.data()}.parent_path() / "D3D12");
}

TEST(DeviceLossTest, RemovedDeviceFailsTheFrameWithADredReport)
{
    const fs::path folder = fs::temp_directory_path() / "meow2-device-loss";
    std::error_code ignored;
    fs::remove_all(folder, ignored); // a log from an earlier run may still be open in a viewer
    fs::path logFile;
    {
        Log::LogSystem logSystem{{.appName = "Test", .directory = folder, .console = true}};
        Log::Logger log = logSystem.createLogger("RHI");
        logFile = logSystem.filePath();
        Result<std::unique_ptr<IRhiDevice>> device =
            createRhiDevice({.backend = RhiBackend::D3D12, .appName = "Meow2 tests", .presentation = false}, log);
        ASSERT_TRUE(device) << device.error().message;
        auto& d3d12 = static_cast<RhiDeviceD3D12&>(**device);

        // A frame with work in it, so DRED has breadcrumbs to report.
        Result<IRhiCommandList*> list = d3d12.beginFrame();
        ASSERT_TRUE(list);
        {
            const ScopedMarker marker{**list, "Before the loss"};
        }
        ASSERT_TRUE((*device)->endFrame());

        // RemoveDevice() is D3D12's test hook: the device behaves exactly as after a GPU crash or driver reset.
        ComPtr<ID3D12Device5> device5;
        ASSERT_TRUE(SUCCEEDED(d3d12.device()->QueryInterface(IID_PPV_ARGS(&device5))));
        device5->RemoveDevice();

        const Result<IRhiCommandList*> lost = d3d12.beginFrame();
        ASSERT_FALSE(lost);
        EXPECT_NE(lost.error().message.find("lost"), std::string::npos) << lost.error().message;
        EXPECT_FALSE(d3d12.beginFrame()) << "every later frame fails too";
        logSystem.flush();
    }
    std::ifstream file{logFile};
    std::stringstream text;
    text << file.rdbuf();
    EXPECT_NE(text.str().find("DRED: "), std::string::npos) << text.str();
    file.close();
    fs::remove_all(folder, ignored);
}
} // namespace Meow2::Rhi
