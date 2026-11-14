/// @file
/// @brief Meow2Renderer's look: a pure-black, high-contrast ImGui theme with one red accent, and its fonts.

#pragma once

#include <imgui.h>

#include <cstddef>
#include <span>
#include <vector>

namespace Meow2::RendererApp
{
/// @brief The palette, shared with Meow2Editor's Qt theme (docs/wiki/Theme.md). Linear 0..1 RGBA, as ImGui takes it.
namespace UiColors
{
inline constexpr ImVec4 c_base{0.0F, 0.0F, 0.0F, 1.0F};               ///< #000000: backgrounds.
inline constexpr ImVec4 c_panel{0.047F, 0.047F, 0.051F, 0.94F};       ///< #0C0C0D: windows, a little see-through.
inline constexpr ImVec4 c_frame{0.094F, 0.094F, 0.106F, 1.0F};        ///< #18181B: fields and buttons.
inline constexpr ImVec4 c_frameHovered{0.133F, 0.133F, 0.149F, 1.0F}; ///< #222226: a field under the mouse.
inline constexpr ImVec4 c_line{0.204F, 0.204F, 0.227F, 1.0F};         ///< #34343A: borders and separators.
inline constexpr ImVec4 c_text{0.902F, 0.902F, 0.902F, 1.0F};         ///< #E6E6E6: text.
inline constexpr ImVec4 c_textDim{0.541F, 0.541F, 0.565F, 1.0F};      ///< #8A8A90: labels, disabled text.
inline constexpr ImVec4 c_accent{0.878F, 0.118F, 0.176F, 1.0F};       ///< #E01E2D: everything active.
inline constexpr ImVec4 c_accentBright{1.0F, 0.353F, 0.4F, 1.0F};     ///< #FF5A66: the accent under the mouse.
} // namespace UiColors

/// @brief The accent at another opacity, for washes behind selected or hovered items.
/// @param alpha 0..1.
/// @return The colour.
[[nodiscard]] constexpr ImVec4 accentWash(float alpha)
{
    return {UiColors::c_accent.x, UiColors::c_accent.y, UiColors::c_accent.z, alpha};
}

/// @brief Sets every size and colour of a style to the Meow2 theme: square corners, 1-pixel borders, near-black
///        panels, light grey text, and the red accent on everything hovered, pressed, selected or focused.
///
/// The style is rebuilt from ImGui's defaults each time, so calling it again for another display scale doesn't
/// compound the scaling.
/// @param style The style to overwrite, usually ImGui::GetStyle().
/// @param scale The display's content scale (1.25 at 125 %); sizes and fonts are multiplied by it. > 0.
void applyUiTheme(ImGuiStyle& style, float scale);

/// @brief Draws the theme's window decorations on the current window: red corner brackets and a red tick at the left
///        of the title bar. Call right after ImGui::Begin() returned true.
void drawWindowAccents();

/// @brief The theme's fonts, once added to ImGui's atlas. Plain data; null for a font that couldn't be loaded, in
///        which case ImGui's built-in font is used.
struct UiFonts
{
    ImFont* text = nullptr; ///< Barlow Semi Condensed: labels, buttons, titles.
    ImFont* mono = nullptr; ///< Consolas (Windows): numbers, paths, log lines.
};

/// @brief The font files to give the UI. Plain data; empty means "not found".
struct UiFontFiles
{
    std::vector<std::byte> text{}; ///< A TrueType file for UiFonts::text.
    std::vector<std::byte> mono{}; ///< A TrueType file for UiFonts::mono.
};

/// @brief Draws the following text in the monospace font, a little smaller than the UI font, until popMonoFont().
/// @param fonts The fonts; with no monospace font this changes nothing (but must still be popped).
void pushMonoFont(const UiFonts& fonts);

/// @brief Ends pushMonoFont().
void popMonoFont();

/// @brief Draws a dashed polyline: `dash` pixels on, `dash` off, measured along the whole line so the pattern runs on
///        across corners. ImGui has no dashed lines. Dashes mark what exists only for debugging (the render graph's
///        debug passes, a debug view that makes its own buffer).
/// @param draw      Where to draw.
/// @param points    The points, in order.
/// @param colour    The colour.
/// @param thickness Line width in pixels.
/// @param dash      Dash and gap length in pixels.
void dashedPolyline(ImDrawList& draw, std::span<const ImVec2> points, ImU32 colour, float thickness, float dash);

/// @brief Draws a dashed rectangle outline (dashedPolyline() around it).
/// @param draw      Where to draw.
/// @param min       Top-left corner.
/// @param max       Bottom-right corner.
/// @param colour    The colour.
/// @param thickness Line width in pixels.
/// @param dash      Dash and gap length in pixels.
void dashedRect(ImDrawList& draw, ImVec2 min, ImVec2 max, ImU32 colour, float thickness, float dash);
} // namespace Meow2::RendererApp
