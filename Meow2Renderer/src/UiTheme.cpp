/// @file
/// @brief The Meow2 ImGui theme (UiTheme.h).

#include "UiTheme.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Meow2::RendererApp
{
namespace
{
/// @brief Consolas' size relative to Barlow's: the same pixel size looks bigger in Consolas.
constexpr float c_monoFontScale = 0.85F;

/// @brief A colour with another opacity.
constexpr ImVec4 withAlpha(const ImVec4& color, float alpha)
{
    return {color.x, color.y, color.z, alpha};
}
} // namespace

void applyUiTheme(ImGuiStyle& style, float scale)
{
    using namespace UiColors;
    // From the defaults every time: ScaleAllSizes multiplies what's there, so scaling twice would compound.
    style = ImGuiStyle{};
    ImGui::StyleColorsDark(&style); // a full set first, so a colour slot added by a later ImGui isn't left unset

    // Flat and square: no rounding anywhere, 1-pixel lines.
    style.WindowRounding = 0.0F;
    style.ChildRounding = 0.0F;
    style.FrameRounding = 0.0F;
    style.PopupRounding = 0.0F;
    style.ScrollbarRounding = 0.0F;
    style.GrabRounding = 0.0F;
    style.TabRounding = 0.0F;
    style.WindowBorderSize = 1.0F;
    style.FrameBorderSize = 1.0F;
    style.PopupBorderSize = 1.0F;
    style.ChildBorderSize = 1.0F;
    style.TabBorderSize = 0.0F;
    style.WindowPadding = ImVec2{10.0F, 10.0F};
    style.FramePadding = ImVec2{8.0F, 4.0F};
    style.ItemSpacing = ImVec2{8.0F, 6.0F};
    style.ItemInnerSpacing = ImVec2{6.0F, 4.0F};
    style.ScrollbarSize = 10.0F;
    style.GrabMinSize = 8.0F;
    style.WindowTitleAlign = ImVec2{0.0F, 0.5F};
    // Section headings read "— GPU ————": a short line, the text, then the rest of the line.
    style.SeparatorTextBorderSize = 1.0F;
    style.SeparatorTextAlign = ImVec2{0.0F, 0.5F};
    style.SeparatorTextPadding = ImVec2{12.0F, 4.0F};

    ImVec4* colors = style.Colors;
    colors[ImGuiCol_Text] = c_text;
    colors[ImGuiCol_TextDisabled] = c_textDim;
    colors[ImGuiCol_WindowBg] = c_panel;
    colors[ImGuiCol_ChildBg] = withAlpha(c_base, 0.0F);
    colors[ImGuiCol_PopupBg] = withAlpha(c_panel, 0.98F);
    colors[ImGuiCol_Border] = c_line;
    colors[ImGuiCol_BorderShadow] = withAlpha(c_base, 0.0F);
    colors[ImGuiCol_FrameBg] = c_frame;
    colors[ImGuiCol_FrameBgHovered] = c_frameHovered;
    colors[ImGuiCol_FrameBgActive] = accentWash(0.25F);
    colors[ImGuiCol_TitleBg] = c_base;
    colors[ImGuiCol_TitleBgActive] = c_base;
    colors[ImGuiCol_TitleBgCollapsed] = withAlpha(c_base, 0.8F);
    colors[ImGuiCol_MenuBarBg] = c_panel;
    colors[ImGuiCol_ScrollbarBg] = withAlpha(c_base, 0.5F);
    colors[ImGuiCol_ScrollbarGrab] = c_line;
    colors[ImGuiCol_ScrollbarGrabHovered] = withAlpha(c_textDim, 0.6F);
    colors[ImGuiCol_ScrollbarGrabActive] = c_accent;
    colors[ImGuiCol_CheckMark] = c_accent;
    colors[ImGuiCol_CheckboxSelectedBg] = c_frame;
    colors[ImGuiCol_SliderGrab] = c_accent;
    colors[ImGuiCol_SliderGrabActive] = c_accentBright;
    // Controls stay grey until the mouse is on them; then red, stronger when pressed.
    colors[ImGuiCol_Button] = c_frame;
    colors[ImGuiCol_ButtonHovered] = accentWash(0.3F);
    colors[ImGuiCol_ButtonActive] = accentWash(0.6F);
    colors[ImGuiCol_Header] = accentWash(0.2F);
    colors[ImGuiCol_HeaderHovered] = accentWash(0.32F);
    colors[ImGuiCol_HeaderActive] = accentWash(0.5F);
    colors[ImGuiCol_Separator] = c_line;
    colors[ImGuiCol_SeparatorHovered] = accentWash(0.6F);
    colors[ImGuiCol_SeparatorActive] = c_accent;
    colors[ImGuiCol_ResizeGrip] = withAlpha(c_line, 0.6F);
    colors[ImGuiCol_ResizeGripHovered] = accentWash(0.6F);
    colors[ImGuiCol_ResizeGripActive] = c_accent;
    colors[ImGuiCol_InputTextCursor] = c_accent;
    colors[ImGuiCol_Tab] = c_panel;
    colors[ImGuiCol_TabHovered] = accentWash(0.32F);
    colors[ImGuiCol_TabSelected] = c_frame;
    colors[ImGuiCol_TabSelectedOverline] = c_accent;
    colors[ImGuiCol_TabDimmed] = c_panel;
    colors[ImGuiCol_TabDimmedSelected] = c_frame;
    colors[ImGuiCol_TabDimmedSelectedOverline] = c_line;
    colors[ImGuiCol_DockingPreview] = accentWash(0.4F);
    colors[ImGuiCol_DockingEmptyBg] = c_base;
    colors[ImGuiCol_PlotLines] = c_accent;
    colors[ImGuiCol_PlotLinesHovered] = c_accentBright;
    colors[ImGuiCol_PlotHistogram] = c_accent;
    colors[ImGuiCol_PlotHistogramHovered] = c_accentBright;
    colors[ImGuiCol_TableHeaderBg] = c_frame;
    colors[ImGuiCol_TableBorderStrong] = c_line;
    colors[ImGuiCol_TableBorderLight] = c_frameHovered;
    colors[ImGuiCol_TableRowBg] = withAlpha(c_base, 0.0F);
    colors[ImGuiCol_TableRowBgAlt] = ImVec4{1.0F, 1.0F, 1.0F, 0.03F};
    colors[ImGuiCol_TextLink] = c_accentBright;
    colors[ImGuiCol_TextSelectedBg] = accentWash(0.35F);
    colors[ImGuiCol_TreeLines] = c_line;
    colors[ImGuiCol_DragDropTarget] = c_accent;
    colors[ImGuiCol_DragDropTargetBg] = accentWash(0.15F);
    colors[ImGuiCol_NavCursor] = c_accent;
    colors[ImGuiCol_NavWindowingHighlight] = accentWash(0.7F);
    colors[ImGuiCol_NavWindowingDimBg] = withAlpha(c_base, 0.6F);
    colors[ImGuiCol_ModalWindowDimBg] = withAlpha(c_base, 0.6F);

    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;
}

void drawWindowAccents()
{
    // Drawn on the window's own list, but over its whole rectangle: the normal clip rectangle leaves out the title
    // bar and the border.
    ImDrawList* const draw = ImGui::GetWindowDrawList();
    const ImVec2 min = ImGui::GetWindowPos();
    const ImVec2 size = ImGui::GetWindowSize();
    const ImVec2 max{min.x + size.x, min.y + size.y};
    const float scale = ImGui::GetStyle().FontScaleDpi;
    const float arm = std::min(10.0F * scale, std::min(size.x, size.y) * 0.25F);
    const float thick = std::max(1.0F, 2.0F * scale);
    const ImU32 red = ImGui::GetColorU32(UiColors::c_accent);
    draw->PushClipRect(min, max, false);
    // Corner brackets: an L at each corner, on the border.
    draw->AddRectFilled(min, ImVec2{min.x + arm, min.y + thick}, red);
    draw->AddRectFilled(min, ImVec2{min.x + thick, min.y + arm}, red);
    draw->AddRectFilled(ImVec2{max.x - arm, min.y}, ImVec2{max.x, min.y + thick}, red);
    draw->AddRectFilled(ImVec2{max.x - thick, min.y}, ImVec2{max.x, min.y + arm}, red);
    draw->AddRectFilled(ImVec2{min.x, max.y - thick}, ImVec2{min.x + arm, max.y}, red);
    draw->AddRectFilled(ImVec2{min.x, max.y - arm}, ImVec2{min.x + thick, max.y}, red);
    draw->AddRectFilled(ImVec2{max.x - arm, max.y - thick}, max, red);
    draw->AddRectFilled(ImVec2{max.x - thick, max.y - arm}, max, red);
    // A tick in front of the title, the height of its text.
    const float titleHeight = ImGui::GetFrameHeight();
    const float tickInset = titleHeight * 0.25F;
    draw->AddRectFilled(ImVec2{min.x + 3.0F * scale, min.y + tickInset},
                        ImVec2{min.x + 3.0F * scale + 3.0F * scale, min.y + titleHeight - tickInset}, red);
    draw->PopClipRect();
}

void pushMonoFont(const UiFonts& fonts)
{
    // ImGui 1.92 draws every font at style.FontSizeBase (times FontScaleDpi), whatever size it was added at. Consolas
    // looks bigger than Barlow at the same size, so it's drawn smaller. A null font keeps the current one.
    ImGui::PushFont(fonts.mono, ImGui::GetStyle().FontSizeBase * c_monoFontScale);
}

void popMonoFont()
{
    ImGui::PopFont();
}

void dashedPolyline(ImDrawList& draw, std::span<const ImVec2> points, ImU32 colour, float thickness, float dash)
{
    float travelled = 0.0F; // along the whole line, so the pattern runs on across corners
    for (std::size_t i = 1; i < points.size(); ++i)
    {
        const ImVec2 a = points[i - 1];
        const ImVec2 b = points[i];
        const float length = std::sqrt((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y));
        float along = 0.0F;
        while (along < length)
        {
            // Where in the pattern this point is: 0..dash on, dash..2 dash off; step to the next change or the end.
            const float phase = std::fmod(travelled, dash * 2.0F);
            const float step = std::min(length - along, (phase < dash ? dash : dash * 2.0F) - phase);
            if (phase < dash)
            {
                const float t0 = along / length;
                const float t1 = (along + step) / length;
                draw.AddLine(ImVec2{a.x + (b.x - a.x) * t0, a.y + (b.y - a.y) * t0},
                             ImVec2{a.x + (b.x - a.x) * t1, a.y + (b.y - a.y) * t1}, colour, thickness);
            }
            along += step;
            travelled += step;
        }
    }
}

void dashedRect(ImDrawList& draw, ImVec2 min, ImVec2 max, ImU32 colour, float thickness, float dash)
{
    const std::array<ImVec2, 5> corners{min, ImVec2{max.x, min.y}, max, ImVec2{min.x, max.y}, min};
    dashedPolyline(draw, corners, colour, thickness, dash);
}
} // namespace Meow2::RendererApp
