/// @file
/// @brief RenderDevice: switching the device's backend while the app runs.

#include "Meow2/Graphics/Renderer/RenderDevice.h"

#include "Meow2/Core/Assert.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <format>
#include <string_view>
#include <utility>

namespace Meow2::Renderer
{
namespace
{
/// @brief A backend's name, for the log.
/// @param backend The backend.
/// @return "Vulkan" or "D3D12".
std::string_view nameOf(Rhi::RhiBackend backend)
{
    return backend == Rhi::RhiBackend::D3D12 ? "D3D12" : "Vulkan";
}
} // namespace

DeviceUser::DeviceUser(RenderDevice* owner, std::uint64_t id)
    : m_owner(owner)
    , m_id(id)
{
}

DeviceUser::~DeviceUser()
{
    reset();
}

DeviceUser::DeviceUser(DeviceUser&& other) noexcept
    : m_owner(std::exchange(other.m_owner, nullptr))
    , m_id(std::exchange(other.m_id, 0))
{
}

DeviceUser& DeviceUser::operator=(DeviceUser&& other) noexcept
{
    if (this != &other)
    {
        reset();
        m_owner = std::exchange(other.m_owner, nullptr);
        m_id = std::exchange(other.m_id, 0);
    }
    return *this;
}

void DeviceUser::reset()
{
    if (m_owner != nullptr)
    {
        m_owner->removeUser(m_id);
        m_owner = nullptr;
        m_id = 0;
    }
}

Result<std::unique_ptr<RenderDevice>> RenderDevice::create(const Rhi::RhiDeviceDesc& desc, Log::Logger& log)
{
    Result<std::unique_ptr<Rhi::IRhiDevice>> device = Rhi::createRhiDevice(desc, log);
    if (!device)
    {
        return Unexpected<Error>{device.error()};
    }
    return std::make_unique<RenderDevice>(desc, log, std::move(*device));
}

RenderDevice::RenderDevice(const Rhi::RhiDeviceDesc& desc, Log::Logger& log, std::unique_ptr<Rhi::IRhiDevice> device)
    : m_desc(desc)
    , m_log(log)
    , m_device(std::move(device))
    , m_backend(desc.backend)
{
    MEOW2_ASSERT(m_device != nullptr, "RenderDevice needs a device");
    // The caller's bytes are only valid during this call; later devices get their cache from m_pipelineCaches.
    m_desc.pipelineCache = {};
}

RenderDevice::~RenderDevice()
{
    MEOW2_ASSERT(m_users.empty(), "A DeviceUser outlived its RenderDevice");
    if (m_device)
    {
        m_device->waitIdle();
    }
}

Rhi::IRhiDevice& RenderDevice::device()
{
    MEOW2_ASSERT(m_device != nullptr, "There's no device: a switch and the way back both failed");
    return *m_device;
}

DeviceUser RenderDevice::addUser(std::string name, DeviceRelease release, DeviceRestore restore)
{
    MEOW2_ASSERT(!m_swapping, "Can't register a device user during a switch");
    const std::uint64_t id = m_nextUserId++;
    m_users.push_back(
        {.id = id, .name = std::move(name), .release = std::move(release), .restore = std::move(restore)});
    return DeviceUser{this, id};
}

void RenderDevice::removeUser(std::uint64_t id)
{
    MEOW2_ASSERT(!m_swapping, "Can't unregister a device user during a switch");
    std::erase_if(m_users, [id](const User& user) { return user.id == id; });
}

std::vector<std::byte>& RenderDevice::cacheOf(Rhi::RhiBackend backend)
{
    return m_pipelineCaches[static_cast<std::size_t>(backend)];
}

void RenderDevice::setPipelineCache(Rhi::RhiBackend backend, std::vector<std::byte> data)
{
    cacheOf(backend) = std::move(data);
}

std::vector<std::byte> RenderDevice::pipelineCache(Rhi::RhiBackend backend)
{
    if (m_device && backend == m_backend)
    {
        return m_device->pipelineCacheData();
    }
    return cacheOf(backend);
}

void RenderDevice::stopDevice()
{
    m_device->waitIdle();
    // Last registered first: a user may hold things made with an earlier one's objects (a scene's pipelines).
    for (auto user = m_users.rbegin(); user != m_users.rend(); ++user)
    {
        user->release(*m_device);
    }
    // Empty under RenderDoc on D3D12 (no pipeline library there): keep what an earlier device compiled instead.
    if (std::vector<std::byte> cache = m_device->pipelineCacheData(); !cache.empty())
    {
        cacheOf(m_backend) = std::move(cache);
    }
    // Waits for the frames destroyed objects were waiting on, then destroys them and the device.
    m_device.reset();
}

Result<void> RenderDevice::startDevice(Rhi::RhiBackend backend)
{
    Rhi::RhiDeviceDesc desc = m_desc;
    desc.backend = backend;
    desc.pipelineCache = cacheOf(backend);
    Result<std::unique_ptr<Rhi::IRhiDevice>> device = Rhi::createRhiDevice(desc, m_log);
    if (!device)
    {
        return Unexpected<Error>{device.error()};
    }
    m_device = std::move(*device);
    m_backend = backend;

    for (std::size_t i = 0; i < m_users.size(); ++i)
    {
        if (Result<void> restored = m_users[i].restore(*m_device); !restored)
        {
            // Undo the ones that did restore, so no user holds objects of a device that's about to go.
            m_device->waitIdle();
            for (std::size_t j = i; j-- > 0;)
            {
                m_users[j].release(*m_device);
            }
            m_device.reset();
            return fail(restored.error().code, std::format("{}: {}", m_users[i].name, restored.error().message));
        }
    }
    return {};
}

Result<void> RenderDevice::swapTo(Rhi::RhiBackend backend)
{
    MEOW2_ASSERT(!m_swapping, "A switch is already running");
    MEOW2_ASSERT(m_device != nullptr, "There's no device to switch from");
    m_swapping = true;
    const auto start = std::chrono::steady_clock::now();
    const Rhi::RhiBackend previous = m_backend;
    MEOW2_LOG_INFO(m_log, "Switching from {} to {}", nameOf(previous), nameOf(backend));

    stopDevice();
    Result<void> started = startDevice(backend);
    if (started)
    {
        m_swapping = false;
        ++m_swapCount;
        const auto took =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
        MEOW2_LOG_INFO(m_log, "Switched to {} in {} ms", nameOf(backend), took.count());
        return {};
    }

    MEOW2_LOG_ERROR(m_log, "Can't switch to {}: {}. Going back to {}", nameOf(backend), started.error(),
                    nameOf(previous));
    Result<void> back = startDevice(previous);
    m_swapping = false;
    if (!back)
    {
        MEOW2_LOG_ERROR(m_log, "Can't go back to {} either: {}", nameOf(previous), back.error());
        return fail(back.error().code,
                    std::format("Can't switch to {} ({}), and can't go back to {} ({})", nameOf(backend),
                                started.error().message, nameOf(previous), back.error().message));
    }
    return fail(started.error().code, std::format("Can't switch to {}: {}", nameOf(backend), started.error().message));
}
} // namespace Meow2::Renderer
