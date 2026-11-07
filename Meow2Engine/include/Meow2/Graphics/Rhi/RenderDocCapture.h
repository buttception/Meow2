/// @file
/// @brief RenderDocCapture: grabs a frame from inside the app when RenderDoc is attached.

#pragma once

#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/SharedLibrary.h>

#include <cstdint>
#include <memory>
#include <optional>

namespace Meow2::Rhi
{
/// @brief Talks to RenderDoc if the app was launched from it, and does nothing otherwise.
///
/// RenderDoc injects its library into the process it launches. This class looks for that library and never loads it, so
/// an ordinary run is unchanged, on any machine. It works with either backend, and needs no device. With it, a key or
/// an MCP command can capture a frame, and the capture lands in RenderDoc's list.
/// @par Thread safety
/// Not thread-safe. Use on the thread that renders.
/// @see techstack.md §11, docs/wiki/Rhi.md
class RenderDocCapture
{
public:
    /// @brief Looks for RenderDoc.
    /// @param log Told whether it was found. Only used during construction.
    explicit RenderDocCapture(Log::Logger& log);

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

    /// @brief How many captures this run has made.
    /// @return The count; 0 without RenderDoc.
    [[nodiscard]] std::uint32_t captureCount() const;

private:
    std::optional<SharedLibrary> m_library;
    const void* m_api = nullptr; ///< RENDERDOC_API_1_6_0. Opaque here, so RenderDoc's header stays out of this one.
};
} // namespace Meow2::Rhi
