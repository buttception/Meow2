/// @file
/// @brief SettingsWindow (SettingsWindow.h).

#include "SettingsWindow.h"

#include <imgui.h>

#include <algorithm>
#include <format>
#include <utility>

namespace Meow2::RendererApp
{
namespace
{
/// @brief The start backends the window offers: config name and label.
constexpr std::array<std::array<std::string_view, 2>, 2> c_backends{{{"vulkan", "VULKAN"}, {"d3d12", "D3D12"}}};

} // namespace

bool readSetting(const Config::Config& config, const BoolSetting& setting)
{
    return config.getBool(setting.key, setting.defaultValue);
}

SettingsWindow::SettingsWindow(Config::Config& config)
    : m_config(config)
    , m_backendAtStart(config.getString(c_backendKey, "vulkan"))
{
    for (std::size_t i = 0; i < c_boolSettings.size(); ++i)
    {
        m_atStart[i] = readSetting(config, c_boolSettings[i]);
    }
}

bool SettingsWindow::set(const BoolSetting& setting, bool value)
{
    m_config.setBool(setting.key, value);
    if (setting.effect == SettingEffect::NextScene &&
        std::ranges::find(m_nextSceneChanges, setting.key) == m_nextSceneChanges.end())
    {
        m_nextSceneChanges.push_back(setting.key);
    }
    // Saved now rather than only at exit, so a change survives a crash (which is what debug settings are for).
    const Result<void> saved = m_config.save();
    m_saveError = saved ? std::string{} : std::format("{}", saved.error());
    return static_cast<bool>(saved);
}

bool SettingsWindow::setStartBackend(std::string_view backend)
{
    m_config.setString(c_backendKey, backend);
    const Result<void> saved = m_config.save();
    m_saveError = saved ? std::string{} : std::format("{}", saved.error());
    return static_cast<bool>(saved);
}

bool SettingsWindow::restartNeeded() const
{
    for (std::size_t i = 0; i < c_boolSettings.size(); ++i)
    {
        if (c_boolSettings[i].effect == SettingEffect::Restart &&
            readSetting(m_config, c_boolSettings[i]) != m_atStart[i])
        {
            return true;
        }
    }
    return m_config.getString(c_backendKey, "vulkan") != m_backendAtStart;
}

std::vector<std::string_view> SettingsWindow::takeNextSceneChanges()
{
    return std::exchange(m_nextSceneChanges, {});
}

void SettingsWindow::draw(const UiFonts& fonts)
{
    const float scale = ImGui::GetStyle().FontScaleDpi;
    ImGui::SetNextWindowPos(ImVec2{360.0F * scale, 10.0F * scale}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2{320.0F * scale, 0.0F}, ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("SETTINGS###Settings"))
    {
        ImGui::End();
        return;
    }
    drawWindowAccents();
    ImGui::SeparatorText("DEBUG TOOLS");
    for (std::size_t i = 0; i < c_boolSettings.size(); ++i)
    {
        const BoolSetting& setting = c_boolSettings[i];
        if (setting.key == c_compressTexturesSetting.key)
        {
            ImGui::SeparatorText("SCENES");
        }
        bool value = readSetting(m_config, setting);
        // The key is the ID, so two settings with the same label never share state.
        ImGui::PushID(setting.key.data(), setting.key.data() + setting.key.size());
        if (ImGui::Checkbox(std::string{setting.label}.c_str(), &value))
        {
            static_cast<void>(set(setting, value));
        }
        ImGui::PopID();
        ImGui::SetItemTooltip("%.*s\n(%.*s)", static_cast<int>(setting.help.size()), setting.help.data(),
                              static_cast<int>(setting.key.size()), setting.key.data());
        const bool changed = value != m_atStart[i] && setting.effect != SettingEffect::Now;
        if (changed)
        {
            ImGui::SameLine();
            ImGui::TextColored(UiColors::c_textDim,
                               setting.effect == SettingEffect::Restart ? "ON RESTART" : "NEXT SCENE");
        }
    }

    ImGui::SeparatorText("START BACKEND");
    const std::string backend = m_config.getString(c_backendKey, "vulkan");
    for (const auto& [name, label] : c_backends)
    {
        if (ImGui::RadioButton(std::string{label}.c_str(), backend == name) && backend != name)
        {
            static_cast<void>(setStartBackend(name));
        }
        ImGui::SameLine();
    }
    ImGui::NewLine();
    ImGui::TextColored(UiColors::c_textDim, "F10 switches while running.");

    if (restartNeeded())
    {
        ImGui::Spacing();
        ImGui::TextColored(UiColors::c_accentBright, "Restart Meow2Renderer to apply the marked settings.");
    }
    if (!m_saveError.empty())
    {
        pushMonoFont(fonts);
        ImGui::TextColored(UiColors::c_accentBright, "Not saved: %s", m_saveError.c_str());
        popMonoFont();
    }
    ImGui::End();
}
} // namespace Meow2::RendererApp
