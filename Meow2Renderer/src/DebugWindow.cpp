/// @file
/// @brief The Debug window (DebugWindow.h).

#include "DebugWindow.h"

#include <imgui.h>

#include <algorithm>
#include <string>
#include <string_view>

namespace Meow2::RendererApp
{
namespace
{
/// @brief The view buttons: one per view, grouped, the active one highlighted.
void drawViewButtons(Renderer::RenderDebugState& state)
{
    // Every view is one click away: a row of equal buttons per group, wrapping to the window's width, the active one
    // in the accent colour.
    const ImGuiStyle& style = ImGui::GetStyle();
    float buttonWidth = 0.0F;
    for (const Renderer::DebugViewEntry& entry : Renderer::debugViewEntries())
    {
        buttonWidth =
            std::max(buttonWidth, ImGui::CalcTextSize(entry.label.data(), entry.label.data() + entry.label.size()).x);
    }
    buttonWidth += style.FramePadding.x * 2.0F;
    const float available = ImGui::GetContentRegionAvail().x;
    const int columns =
        std::max(1, static_cast<int>((available + style.ItemSpacing.x) / (buttonWidth + style.ItemSpacing.x)));
    buttonWidth = std::max(buttonWidth, (available - style.ItemSpacing.x * static_cast<float>(columns - 1)) /
                                            static_cast<float>(columns)); // fill the row
    std::string_view group;
    int column = 0;
    for (const Renderer::DebugViewEntry& entry : Renderer::debugViewEntries())
    {
        if (entry.group != group)
        {
            group = entry.group;
            column = 0;
            ImGui::TextColored(UiColors::c_textDim, "%.*s", static_cast<int>(group.size()), group.data());
        }
        if (column > 0)
        {
            ImGui::SameLine();
        }
        const bool active = state.view == entry.view;
        if (active)
        {
            ImGui::PushStyleColor(ImGuiCol_Button, UiColors::c_accent);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, UiColors::c_accentBright);
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, UiColors::c_accentBright);
        }
        ImGui::PushID(entry.view);
        if (ImGui::Button(std::string{entry.label}.c_str(), ImVec2{buttonWidth, 0.0F}))
        {
            state.view = entry.view;
        }
        if (entry.generated)
        {
            // A dotted outline: this view shows a buffer a debug-only pass makes for it, not one the frame has.
            const float inset = 2.0F;
            const ImVec2 min = ImGui::GetItemRectMin();
            const ImVec2 max = ImGui::GetItemRectMax();
            dashedRect(*ImGui::GetWindowDrawList(), ImVec2{min.x + inset, min.y + inset},
                       ImVec2{max.x - inset, max.y - inset},
                       ImGui::GetColorU32(active ? UiColors::c_text : UiColors::c_textDim), 1.0F, 3.0F);
            ImGui::SetItemTooltip("Shown from a buffer made only for this view (dotted): a debug pass rebuilds it, "
                                  "and nothing in the frame uses it.");
        }
        ImGui::PopID();
        if (active)
        {
            ImGui::PopStyleColor(3);
        }
        column = (column + 1) % columns;
    }
}
} // namespace

void drawDebugWindow(Renderer::RenderDebug& debug, const UiFonts& fonts)
{
    if (!ImGui::Begin("DEBUG###Debug"))
    {
        ImGui::End();
        return;
    }
    Renderer::RenderDebugState& state = debug.state();
    drawWindowAccents();
    ImGui::SeparatorText("VIEW");
    drawViewButtons(state);
    if (const Renderer::DebugViewEntry* entry = Renderer::findDebugView(state.view);
        entry != nullptr && !entry->hint.empty())
    {
        ImGui::PushTextWrapPos(0.0F);
        ImGui::TextColored(UiColors::c_textDim, "%.*s", static_cast<int>(entry->hint.size()), entry->hint.data());
        ImGui::PopTextWrapPos();
    }
    ImGui::SeparatorText("WIREFRAME");
    int wireframe = static_cast<int>(state.wireframe);
    ImGui::RadioButton("OFF", &wireframe, static_cast<int>(Renderer::WireframeMode::Off));
    ImGui::SameLine();
    ImGui::RadioButton("OVERLAY", &wireframe, static_cast<int>(Renderer::WireframeMode::Overlay));
    ImGui::SameLine();
    ImGui::RadioButton("ONLY", &wireframe, static_cast<int>(Renderer::WireframeMode::Only));
    state.wireframe = static_cast<Renderer::WireframeMode>(wireframe);
    ImGui::SeparatorText("CULLING");
    ImGui::Checkbox("FREEZE", &state.freezeCulling);
    ImGui::SetItemTooltip("Keep culling with the camera's frustum from now: fly away and see what was culled.");
    ImGui::SameLine();
    ImGui::Checkbox("SHOW FRUSTUM", &state.showFrustum);
    ImGui::SetItemTooltip("While frozen, draw the frozen camera's view volume (yellow), through the scene.");
    if (debug.failed())
    {
        pushMonoFont(fonts);
        ImGui::TextColored(UiColors::c_accentBright, "Debug pipelines failed to build; see the console.");
        popMonoFont();
    }
    ImGui::End();
}
} // namespace Meow2::RendererApp
