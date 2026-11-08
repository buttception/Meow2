#pragma once

/// @file
/// @brief Meow2Editor's look: the same pure-black, high-contrast theme with a red accent as Meow2Renderer
///        (docs/wiki/Theme.md), as a Qt palette, a style sheet and fonts.

#include <Meow2/Core/Log/Logger.h>

#include <filesystem>
#include <QApplication>
#include <QColor>
#include <QFont>
#include <QPalette>
#include <QString>

namespace Meow2::Editor
{
/// @brief The palette, shared with Meow2Renderer's ImGui theme (Meow2Renderer/src/UiTheme.h).
namespace EditorColors
{
inline constexpr QRgb c_base = 0xFF000000;         ///< #000000: backgrounds.
inline constexpr QRgb c_panel = 0xFF0C0C0D;        ///< #0C0C0D: docks, lists, menus.
inline constexpr QRgb c_frame = 0xFF18181B;        ///< #18181B: fields and buttons.
inline constexpr QRgb c_frameHovered = 0xFF222226; ///< #222226: a row or field under the mouse.
inline constexpr QRgb c_line = 0xFF34343A;         ///< #34343A: borders and separators.
inline constexpr QRgb c_text = 0xFFE6E6E6;         ///< #E6E6E6: text.
inline constexpr QRgb c_textDim = 0xFF8A8A90;      ///< #8A8A90: labels, disabled text.
inline constexpr QRgb c_accent = 0xFFE01E2D;       ///< #E01E2D: everything active.
inline constexpr QRgb c_accentBright = 0xFFFF5A66; ///< #FF5A66: the accent as text, on dark backgrounds.
} // namespace EditorColors

/// @brief The family name of the UI font, bundled in `fonts/` next to the executable.
inline constexpr const char* c_uiFontFamily = "Barlow Semi Condensed";

/// @brief The theme's colours as a Qt palette: what widgets the style sheet doesn't cover use.
/// @return Near-black window and base colours, light text, red highlight; disabled text dimmed.
[[nodiscard]] QPalette editorPalette();

/// @brief The theme's style sheet: square corners, 1-pixel lines, red on hover, focus and selection, dock titles with a
///        red bar, the selected tab underlined in red.
/// @return The style sheet text.
[[nodiscard]] QString editorStyleSheet();

/// @brief The font for console output, logs and numbers: Consolas, or the system's fixed-width font where it's
///        missing.
/// @param pointSize Its size in points.
/// @return The font.
[[nodiscard]] QFont editorMonospaceFont(int pointSize = 10);

/// @brief Applies the theme to the whole application: the Fusion style (the same on every platform, and the one that
///        honours palettes), the palette, the style sheet, and Barlow Semi Condensed as the default font.
/// @param application The application, before its windows are shown.
/// @param fontFolder  The folder holding `BarlowSemiCondensed/` (`fonts/` next to the executable). A missing font
///                    is logged and the system font is kept.
/// @param log         Receives what couldn't be loaded.
void applyEditorTheme(QApplication& application, const std::filesystem::path& fontFolder, Log::Logger& log);

/// @brief WCAG contrast ratio between two colours, from 1 (none) to 21 (black on white).
/// @param a One colour.
/// @param b The other.
/// @return (lighter + 0.05) / (darker + 0.05) of their relative luminances.
[[nodiscard]] double contrastRatio(const QColor& a, const QColor& b);
} // namespace Meow2::Editor
