/// @file
/// @brief RenderDocCapture: grabs a frame from inside the app when RenderDoc is attached.

#pragma once

#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/SharedLibrary.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>

namespace Meow2::Rhi
{
/// @brief How to reach RenderDoc. Plain data.
struct RenderDocDesc
{
    bool load = false;               ///< Load RenderDoc into this process if it isn't there already.
    std::filesystem::path library{}; ///< Its library; empty uses the default install location.
    bool openWindow = true;          ///< Open the RenderDoc window on each capture.
};

/// @brief Talks to RenderDoc, so a frame can be captured from inside the app and opened straight away.
///
/// There are two ways in. Launched from RenderDoc, its library is already in the process, and this class finds it. Or,
/// with RenderDocDesc::load, this class loads the library itself, and there's no need to start RenderDoc first. Without
/// either, it does nothing, so an ordinary run is unchanged on any machine. It works with either backend.
/// @warning RenderDoc has to be in the process before a device exists, so create this first.
/// @par Thread safety
/// Not thread-safe. Use on the thread that renders.
/// @see techstack.md §11, docs/wiki/Rhi.md
class RenderDocCapture
{
public:
    /// @brief Looks for RenderDoc, and loads it if asked to.
    /// @param log  Told what happens. Must outlive this object.
    /// @param desc What to do.
    explicit RenderDocCapture(Log::Logger& log, const RenderDocDesc& desc = {});

    /// @brief Leaves RenderDoc as it was.
    ~RenderDocCapture();

    RenderDocCapture(const RenderDocCapture&) = delete;            ///< Not copyable: holds the library.
    RenderDocCapture& operator=(const RenderDocCapture&) = delete; ///< Not copyable: holds the library.

    /// @brief Whether RenderDoc is attached.
    /// @return True if capture() will do something.
    [[nodiscard]] bool available() const
    {
        return m_api != nullptr;
    }

    /// @brief Captures the next whole frame. Does nothing without RenderDoc.
    void capture();

    /// @brief Notices finished captures and opens the newest in the RenderDoc window. Call once per frame.
    void update();

    /// @brief How many captures this run has made.
    /// @return The count; 0 without RenderDoc.
    [[nodiscard]] std::uint32_t captureCount() const;

private:
    /// @brief Shows the newest capture in the RenderDoc window, starting it if needed.
    /// @param index The capture.
    void open(std::uint32_t index);

    Log::Logger& m_log;
    bool m_openWindow = true;
    std::uint32_t m_opened = 0; ///< Captures already shown.
    std::optional<SharedLibrary> m_library;
    const void* m_api = nullptr; ///< RENDERDOC_API_1_6_0. Opaque here, so RenderDoc's header stays out of this one.
};
} // namespace Meow2::Rhi
