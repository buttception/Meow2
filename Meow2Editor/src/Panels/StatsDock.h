#pragma once

/// @file
/// @brief StatsDock: frame times, the GPU and its memory, the scene's counts and memory per tag.

#include "Commands/EditorCommandRegistry.h"

#include <QDockWidget>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QString>
#include <vector>

class QLabel;
class QTableWidget;

namespace Meow2::Editor
{
/// @brief A small line graph of frame times, newest on the right, scaled to the slowest frame shown.
/// @par Thread safety
/// Not thread-safe; the GUI thread.
class FrameGraph final : public QWidget
{
public:
    /// @brief An empty graph.
    /// @param parent Its parent (which owns it).
    explicit FrameGraph(QWidget* parent);

    /// @brief Sets the frame times to draw and repaints.
    /// @param milliseconds Oldest first.
    void setFrames(std::vector<float> milliseconds);

    /// @brief The frames it draws.
    /// @return Milliseconds, oldest first.
    [[nodiscard]] const std::vector<float>& frames() const
    {
        return m_frames;
    }

protected:
    /// @brief Draws the line, and a guide at 16.7 ms (60 frames a second) when it's in range.
    /// @param event Unused.
    void paintEvent(QPaintEvent* event) override;

private:
    std::vector<float> m_frames;
};

/// @brief A dock with what the editor costs while it runs: frame time and FPS with a graph of the last few seconds,
///        the graphics API, GPU and its memory, validation errors, the scene's counts, and memory per tag (CPU and
///        GPU), as Meow2Renderer's Stats window shows them.
///
/// It reads the `stats.frame` query (so an agent sees the same numbers), at most four times a second.
/// @par Thread safety
/// Not thread-safe; the GUI thread.
class StatsDock final : public QDockWidget
{
public:
    /// @brief The dock; refresh() fills it.
    /// @param commands Runs the `stats.frame` query.
    /// @param parent   The main window.
    explicit StatsDock(EditorCommandRegistry& commands, QWidget* parent = nullptr);

    /// @brief Shows the latest figures, at most four times a second, and only while the dock can be seen.
    /// @param force Refresh now, whatever the time and visibility (tests).
    void refresh(bool force = false);

private:
    /// @brief Fills the labels, graph and table from a `stats.frame` answer.
    void show(const QJsonObject& stats);

    EditorCommandRegistry& m_commands;
    QElapsedTimer m_sinceRefresh; ///< Time since the last refresh; invalid before the first.
    QLabel* m_frame = nullptr;    ///< "16.7 ms (60 FPS)".
    QLabel* m_worst = nullptr;    ///< The slowest frame shown.
    FrameGraph* m_graph = nullptr;
    QLabel* m_gpu = nullptr;          ///< API, GPU, memory, validation errors.
    QLabel* m_scene = nullptr;        ///< Entities, instances, meshes, materials, textures, path.
    QTableWidget* m_memory = nullptr; ///< Tag, CPU, GPU.
    QLabel* m_untracked = nullptr;    ///< Says CPU memory isn't tracked in this build.
};

/// @brief A byte count for people: "512 B", "3.2 KiB", "48.0 MiB", "1.25 GiB".
/// @param bytes The count.
/// @return The text.
[[nodiscard]] QString formatBytes(double bytes);
} // namespace Meow2::Editor
