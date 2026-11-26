#pragma once

/// @file
/// @brief CaptureTools: the frame capture (F9) as editor commands, and the captured graph as JSON both ways.

#include "Commands/EditorCommandRegistry.h"

#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Graphics/Renderer/FrameCapture.h>
#include <Meow2/Graphics/Renderer/FrameFlow.h>

#include <cstdint>
#include <optional>
#include <QJsonObject>

namespace Meow2::Editor
{
class EditorRenderer;

/// @brief A FrameFlow as JSON: `passes` (name, debug, skipped, accesses with resource, state, writes) and
///        `resources` (name, texture, width, height, format, kind, debug).
/// @param flow The flow.
/// @return The object.
[[nodiscard]] QJsonObject flowToJson(const Renderer::FrameFlow& flow);

/// @brief The FrameFlow a flowToJson() object describes (what the Render graph dock draws).
/// @param json The object.
/// @return The flow; resources and accesses out of range are dropped.
[[nodiscard]] Renderer::FrameFlow flowFromJson(const QJsonObject& json);

/// @brief The editor's frame capture behind commands, so the Render graph dock and (PR 6) MCP go the same way:
///
/// - `capture.frame`: captures the next frame drawn (F9);
/// - `capture.clear`: drops the capture;
/// - `capture.info` (query) {brief?}: whether there is a capture, its frame number and, unless `brief`, its graph
///   (flowToJson()) and snapshots;
/// - `capture.show` {pass, resource, channels?, rangeMin?, rangeMax?, maxSize?}: asks for the texture `pass` left of
///   `resource` as a picture (read back from the GPU a frame or two later);
/// - `capture.image` (query) {data?}: the last picture: its serial number, snapshot, size and settings, and with
///   `data` the picture as a base64 PNG.
///
/// Nothing is undoable: a capture is a look at a frame, not a change to the scene. Without a GPU the commands that
/// need one fail with a message.
/// @par Thread safety
/// Not thread-safe; the main thread.
/// @par Lifetime
/// Registers its commands on construction and removes them on destruction; the renderer (if any) must outlive it.
class CaptureTools
{
public:
    /// @brief Registers the commands.
    /// @param commands Where to register them.
    /// @param renderer The renderer whose frames are captured; null without a GPU.
    /// @param log      Receives failures.
    CaptureTools(EditorCommandRegistry& commands, EditorRenderer* renderer, Log::Logger& log);

    /// @brief Removes the commands.
    ~CaptureTools();

    CaptureTools(const CaptureTools&) = delete;            ///< Not copyable: registered commands point at it.
    CaptureTools& operator=(const CaptureTools&) = delete; ///< Not copyable.

    /// @brief Takes a picture that came back from the GPU, after a frame was drawn.
    void tick();

private:
    /// @brief Registers the commands.
    void registerCommands();

    EditorCommandRegistry& m_commands;
    EditorRenderer* m_renderer;
    Log::Logger& m_log;
    std::optional<Renderer::CapturedImage> m_image; ///< The last picture.
    std::uint64_t m_imageSerial = 0;                ///< Counts pictures, so a UI knows when to fetch the next.
};
} // namespace Meow2::Editor
