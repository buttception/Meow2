/// @file
/// @brief RenderDevice: the current GPU device, and switching it to another graphics API while the app runs.

#pragma once

#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Result.h>
#include <Meow2/Graphics/Rhi/IRhiDevice.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace Meow2::Renderer
{
class RenderDevice;

/// @brief Lets go of a user's GPU objects. Called with the device they were made on, after the GPU is idle.
using DeviceRelease = std::function<void(Rhi::IRhiDevice& device)>;

/// @brief Makes a user's GPU objects again on a new device, from what the user kept on the CPU.
using DeviceRestore = std::function<Result<void>(Rhi::IRhiDevice& device)>;

/// @brief A registration with RenderDevice::addUser(). Unregisters when destroyed. Move-only.
///
/// Declare it after the members its callbacks use, so it's destroyed first.
/// @par Thread safety
/// Not thread-safe; use it on the thread that renders.
/// @par Lifetime
/// The RenderDevice must outlive it (asserted when the RenderDevice is destroyed).
class DeviceUser
{
public:
    /// @brief An empty registration.
    DeviceUser() = default;

    /// @brief Unregisters, if registered.
    ~DeviceUser();

    /// @brief Takes over another registration; `other` becomes empty.
    /// @param other Registration to move from.
    DeviceUser(DeviceUser&& other) noexcept;

    /// @brief Unregisters this one, then takes over `other`, which becomes empty.
    /// @param other Registration to move from.
    /// @return `*this`.
    DeviceUser& operator=(DeviceUser&& other) noexcept;

    DeviceUser(const DeviceUser&) = delete;            ///< Not copyable: one registration, one owner.
    DeviceUser& operator=(const DeviceUser&) = delete; ///< Not copyable: one registration, one owner.

    /// @brief Unregisters now.
    void reset();

    /// @brief Whether this is still registered.
    /// @return False when empty, reset or moved from.
    [[nodiscard]] bool active() const
    {
        return m_owner != nullptr;
    }

private:
    friend class RenderDevice;

    /// @brief Made by RenderDevice::addUser().
    /// @param owner The device.
    /// @param id    The registration.
    DeviceUser(RenderDevice* owner, std::uint64_t id);

    RenderDevice* m_owner = nullptr;
    std::uint64_t m_id = 0;
};

/// @brief Owns the GPU device and switches it to another backend while the app runs (techstack.md §11).
///
/// Everything that makes GPU objects registers as a user with two callbacks. A switch then:
/// 1. waits for the GPU to finish everything;
/// 2. has every user release its objects, last registered first;
/// 3. keeps the old device's pipeline cache, and destroys the device;
/// 4. creates the new device, with that backend's pipeline cache;
/// 5. has every user restore its objects, first registered first, from what it kept on the CPU.
///
/// The users are the registry of GPU resources: each keeps the CPU-side description of what it made (pipeline requests,
/// buffer sizes and contents) and makes it again on request. Handles change on the way, so nothing may keep a handle,
/// bindless index or IRhiDevice reference across a switch; fetch them from their owner each frame.
///
/// If the new device can't start, or a user can't restore, the users that did restore are released again and the old
/// backend starts again, so a failed switch leaves the app as it was. Only if that fails too is there no device.
/// @par Thread safety
/// Not thread-safe. Use on the thread that renders, between frames.
/// @par Lifetime
/// Must outlive every DeviceUser and everything holding a reference to it.
/// @see docs/wiki/Rhi.md
class RenderDevice
{
public:
    /// @brief Creates the first device.
    /// @param desc Backend and options. Its `pipelineCache` is used for that backend; see setPipelineCache() for the
    ///             others.
    /// @param log  Receives device messages and switches. Must outlive this object.
    /// @return The device, or the error from creating it.
    [[nodiscard]] static Result<std::unique_ptr<RenderDevice>> create(const Rhi::RhiDeviceDesc& desc, Log::Logger& log);

    /// @brief Use create().
    /// @param desc   Options for every device it creates.
    /// @param log    Receives device messages and switches.
    /// @param device The first device.
    RenderDevice(const Rhi::RhiDeviceDesc& desc, Log::Logger& log, std::unique_ptr<Rhi::IRhiDevice> device);

    /// @brief Destroys the device.
    /// @pre Every DeviceUser is gone (asserted).
    ~RenderDevice();

    RenderDevice(const RenderDevice&) = delete;            ///< Not copyable: owns the device.
    RenderDevice& operator=(const RenderDevice&) = delete; ///< Not copyable: owns the device.

    /// @brief The current device. Don't keep the reference across swapTo().
    /// @return The device.
    /// @pre hasDevice() (asserted).
    [[nodiscard]] Rhi::IRhiDevice& device();

    /// @brief Whether there's a device: false only after a switch and the way back both failed.
    /// @return True while there's a device.
    [[nodiscard]] bool hasDevice() const
    {
        return m_device != nullptr;
    }

    /// @brief The backend running now.
    /// @return The backend; after a failed switch, the one that came back.
    [[nodiscard]] Rhi::RhiBackend backend() const
    {
        return m_backend;
    }

    /// @brief How many switches have succeeded.
    /// @return The count.
    [[nodiscard]] std::uint32_t swapCount() const
    {
        return m_swapCount;
    }

    /// @brief Registers something that makes GPU objects, so it can let go of them and make them again.
    /// @param name    Shown in the log when its restore fails.
    /// @param release Destroys its objects. Must not fail.
    /// @param restore Makes them again on the new device. An error makes the switch fail and go back; a restore that
    ///                fails destroys what it made itself first, because it isn't released afterwards.
    /// @return The registration; the callbacks run until it's destroyed.
    /// @pre No switch is running (asserted).
    /// @note Register in dependency order: a user is restored after, and released before, every user registered
    ///       earlier (a scene after the pipeline library it builds pipelines with).
    [[nodiscard]] DeviceUser addUser(std::string name, DeviceRelease release, DeviceRestore restore);

    /// @brief Switches to another backend, on the same windows. Recreating on the same backend works too.
    /// @param backend The backend to switch to.
    /// @return An error if the switch failed; the old backend is then running again (check hasDevice()).
    /// @pre No frame is being recorded, and no switch is running (asserted).
    [[nodiscard]] Result<void> swapTo(Rhi::RhiBackend backend);

    /// @brief Sets the pipeline cache for a backend that isn't running, from a file of an earlier run.
    /// @param backend The backend.
    /// @param data    From pipelineCache(). Ignored by the driver if it's from another GPU or driver.
    void setPipelineCache(Rhi::RhiBackend backend, std::vector<std::byte> data);

    /// @brief A backend's pipeline cache, to save for the next run.
    /// @param backend The backend.
    /// @return For the running backend, what its device has compiled; for another, what it had when it stopped (or what
    ///         setPipelineCache() set). Empty if there's nothing.
    [[nodiscard]] std::vector<std::byte> pipelineCache(Rhi::RhiBackend backend);

private:
    friend class DeviceUser;

    /// @brief One registered user.
    struct User
    {
        std::uint64_t id = 0;  ///< Matches DeviceUser::m_id.
        std::string name;      ///< For the log.
        DeviceRelease release; ///< Lets go of its objects.
        DeviceRestore restore; ///< Makes them again.
    };

    /// @brief Waits for the GPU, releases every user, keeps the pipeline cache and destroys the device.
    void stopDevice();

    /// @brief Creates a device and restores every user on it. On failure there's no device and no user holds objects.
    /// @param backend The backend.
    /// @return An error from the device or from a user.
    [[nodiscard]] Result<void> startDevice(Rhi::RhiBackend backend);

    /// @brief Removes a registration. Called by DeviceUser.
    /// @param id The registration.
    void removeUser(std::uint64_t id);

    /// @brief Where a backend's pipeline cache is kept.
    /// @param backend The backend.
    /// @return Its slot.
    [[nodiscard]] std::vector<std::byte>& cacheOf(Rhi::RhiBackend backend);

    Rhi::RhiDeviceDesc m_desc;
    Log::Logger& m_log;
    std::unique_ptr<Rhi::IRhiDevice> m_device;
    Rhi::RhiBackend m_backend;
    std::array<std::vector<std::byte>, 2> m_pipelineCaches; ///< Per RhiBackend.
    std::vector<User> m_users;
    std::uint64_t m_nextUserId = 1;
    std::uint32_t m_swapCount = 0;
    bool m_swapping = false;
};
} // namespace Meow2::Renderer
