#pragma once

/// @file
/// @brief RenderGraphDock: the frame capture (F9) in Qt: the captured frame's render graph, its passes, and any
///        texture as a pass left it.

#include "Commands/EditorCommandRegistry.h"

#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Graphics/Renderer/FrameCapture.h>
#include <Meow2/Graphics/Renderer/FrameFlow.h>

#include <array>
#include <cstdint>
#include <optional>
#include <QDockWidget>
#include <QJsonObject>
#include <QString>

class QDoubleSpinBox;
class QLabel;
class QListWidget;
class QToolButton;

namespace Meow2::Editor
{
class FlowView;

/// @brief A dock with the frame capture: **Capture frame** (F9) keeps one frame; its render graph is drawn as a node
///        graph (FlowView); clicking a pass lists what it reads and writes, and clicking a pin or a listed texture
///        shows that texture as the pass left it, with channel and value-range controls.
///
/// No live view (owner decision): the graph is the captured frame's, and textures are pictures read back from the
/// GPU, a frame or two after they're asked for. Everything goes through the `capture.*` commands and queries, so an
/// agent sees and does the same.
/// @par Thread safety
/// Not thread-safe; the GUI thread.
class RenderGraphDock final : public QDockWidget
{
public:
    /// @brief The dock, empty until a capture.
    /// @param commands Runs the `capture.*` commands and queries.
    /// @param log      Receives failed commands.
    /// @param parent   The main window.
    RenderGraphDock(EditorCommandRegistry& commands, Log::Logger& log, QWidget* parent = nullptr);

    /// @brief Shows a new capture or a picture that came back. Cheap when nothing changed (two small queries).
    void refresh();

    /// @brief The node graph, for tests.
    /// @return It.
    [[nodiscard]] FlowView& flowView()
    {
        return *m_view;
    }

    /// @brief Selects a pin, as clicking it does: its pass, and its texture (as the pass that wrote it left it).
    /// @param pin The pin.
    void selectPin(const Renderer::FlowPin& pin);

    /// @brief Selects a pass, as clicking its node does: lists what it reads and writes.
    /// @param pass The pass.
    void selectPass(std::uint32_t pass);

private:
    /// @brief Runs a command; a failure is logged.
    void run(const QString& command, const QJsonObject& parameters);

    /// @brief Asks for the texture `pass` left of `resource`, with the current channel and range controls.
    void requestTexture();

    /// @brief Shows a texture, starting from its default display settings.
    void showTexture(std::uint32_t pass, std::uint32_t resource);

    /// @brief Fills the pass's list of reads and writes.
    void listPass();

    EditorCommandRegistry& m_commands;
    Log::Logger& m_log;
    FlowView* m_view = nullptr;
    QLabel* m_status = nullptr;               ///< "Frame N: X passes, Y textures kept", or how to capture.
    QLabel* m_passTitle = nullptr;            ///< The selected pass.
    QListWidget* m_accesses = nullptr;        ///< Its reads and writes; a texture entry shows it.
    QLabel* m_textureTitle = nullptr;         ///< "<resource> after <pass>".
    QLabel* m_preview = nullptr;              ///< The picture.
    std::array<QToolButton*, 4> m_channels{}; ///< R, G, B, A.
    QDoubleSpinBox* m_rangeMin = nullptr;
    QDoubleSpinBox* m_rangeMax = nullptr;

    double m_shownFrame = -1.0;                 ///< The capture's frame number shown; -1 for none.
    std::optional<std::uint32_t> m_pass;        ///< The selected pass.
    std::optional<std::uint32_t> m_texturePass; ///< The pass whose output is shown.
    std::uint32_t m_textureResource = 0;        ///< Its resource.
    double m_imageSerial = -1.0;                ///< The capture.image serial shown.
};
} // namespace Meow2::Editor
