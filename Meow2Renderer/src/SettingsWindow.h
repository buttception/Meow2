/// @file
/// @brief Meow2Renderer's Settings window: the debug tools and start-up options, saved to the TOML config.

#pragma once

#include "UiTheme.h"

#include <Meow2/Core/Config/Config.h>
#include <Meow2/Core/Platform.h>

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace Meow2::RendererApp
{
/// @brief When a changed setting takes effect.
enum class SettingEffect
{
    Now,       ///< At once: it's read every frame.
    NextScene, ///< When the next scene opens.
    Restart    ///< When Meow2Renderer starts again (it's read while the device or window is made).
};

/// @brief An on/off setting in the config. Plain data; the table below is the only place a default is written down,
///        so the app and the Settings window always agree.
struct BoolSetting
{
    std::string_view key{};                        ///< Dotted config key, e.g. `rhi.vsync`.
    std::string_view label{};                      ///< Shown in the window.
    std::string_view help{};                       ///< Tooltip.
    bool defaultValue = false;                     ///< When the config doesn't say.
    SettingEffect effect = SettingEffect::Restart; ///< When a change applies.
};

/// @brief `rhi.validation`: the Vulkan validation layers / D3D12 debug layer. On except in Release.
inline constexpr BoolSetting c_validationSetting{"rhi.validation", "VALIDATION LAYER",
                                                 "Vulkan validation layers or the D3D12 debug layer: slower, but "
                                                 "reports API misuse to the log.",
                                                 MEOW2_CONFIG_RELEASE == 0, SettingEffect::Restart};
/// @brief `rhi.vsync`: wait for the display's refresh.
inline constexpr BoolSetting c_vsyncSetting{"rhi.vsync", "VSYNC",
                                            "Wait for the display's refresh; off shows the "
                                            "real frame time.",
                                            true, SettingEffect::Restart};
/// @brief `rhi.renderDoc`: load RenderDoc's library at start, so F11 captures a frame.
inline constexpr BoolSetting c_renderDocSetting{"rhi.renderDoc", "RENDERDOC",
                                                "Load RenderDoc at start, so CAPTURE (F11) works without launching "
                                                "from RenderDoc.",
                                                false, SettingEffect::Restart};
/// @brief `rhi.renderDocOpenWindow`: open RenderDoc's window after a capture.
inline constexpr BoolSetting c_renderDocWindowSetting{"rhi.renderDocOpenWindow", "OPEN CAPTURES",
                                                      "Open RenderDoc's window with the capture after F11.", true,
                                                      SettingEffect::Restart};
/// @brief `renderer.compressTextures`: cook glTF textures to BC7/BC5. Off in Debug, where the encoder is very slow.
inline constexpr BoolSetting c_compressTexturesSetting{"renderer.compressTextures", "COMPRESS TEXTURES",
                                                       "Cook textures to BC7 and BC5 (cached in cache/textures). "
                                                       "Slow in Debug builds.",
                                                       MEOW2_CONFIG_DEBUG == 0, SettingEffect::NextScene};
/// @brief `renderer.sky`: Hillaire's sky and image-based lighting; off for a flat ambient light.
inline constexpr BoolSetting c_skySetting{"renderer.sky", "SKY",
                                          "The physically based sky and the lighting from it; "
                                          "off for a flat ambient light.",
                                          true, SettingEffect::Restart};

/// @brief `renderer.meshlets`: the meshlet path (mesh shaders, culling per cluster) where the GPU has mesh shaders.
inline constexpr BoolSetting c_meshletsSetting{"renderer.meshlets", "MESHLETS",
                                               "Draw through mesh shaders, culling each cluster of up to 124 "
                                               "triangles, where the GPU has them; off for one indirect draw per "
                                               "object.",
                                               true, SettingEffect::Now};

/// @brief Every setting the window shows, in its order.
inline constexpr std::array<BoolSetting, 7> c_boolSettings{
    c_validationSetting,       c_vsyncSetting, c_renderDocSetting, c_renderDocWindowSetting,
    c_compressTexturesSetting, c_skySetting,   c_meshletsSetting};

/// @brief Where ImGui's window layout is kept in the config (ImGuiLayer::loadLayout(), takeLayoutChange()).
inline constexpr std::string_view c_uiLayoutKey = "renderer.uiLayout";

/// @brief `rhi.backend`'s key: the graphics API at start (`vulkan` or `d3d12`; F10 switches while running).
inline constexpr std::string_view c_backendKey = "rhi.backend";

/// @brief A setting's value: the config's, or its default.
/// @param config  The app's config.
/// @param setting The setting.
/// @return The value.
[[nodiscard]] bool readSetting(const Config::Config& config, const BoolSetting& setting);

/// @brief The Settings window.
///
/// Every change is written to the config and saved at once, so it survives a crash; settings that apply on restart
/// are marked until then.
/// @par Thread safety
/// Not thread-safe; use on the main thread.
/// @par Lifetime
/// The Config must outlive the window.
class SettingsWindow
{
public:
    /// @brief A window over a config. Remembers the values the app started with, to tell what needs a restart.
    /// @param config The app's config.
    explicit SettingsWindow(Config::Config& config);

    /// @brief Changes a setting, as the window does when it's clicked: writes and saves the config.
    /// @param setting The setting.
    /// @param value   Its new value.
    /// @return Whether the config could be saved (it's changed either way, and saved at exit too).
    bool set(const BoolSetting& setting, bool value);

    /// @brief Changes the start backend (`vulkan` or `d3d12`), as the window does.
    /// @param backend The backend's config name.
    /// @return Whether the config could be saved.
    bool setStartBackend(std::string_view backend);

    /// @brief Whether some changed setting only applies after a restart.
    /// @return True if a restart is needed for what's shown to be true.
    [[nodiscard]] bool restartNeeded() const;

    /// @brief Takes the keys of the settings that changed and apply to the next scene, since the last call.
    /// @return Their keys; the app reads the new values from the config.
    [[nodiscard]] std::vector<std::string_view> takeNextSceneChanges();

    /// @brief Draws the window.
    /// @param fonts The theme's fonts.
    void draw(const UiFonts& fonts = {});

private:
    Config::Config& m_config;                            ///< Where settings are read and written.
    std::array<bool, c_boolSettings.size()> m_atStart{}; ///< Values when the app started.
    std::string m_backendAtStart;                        ///< `rhi.backend` when the app started.
    std::vector<std::string_view> m_nextSceneChanges;    ///< Not yet taken by the app.
    std::string m_saveError;                             ///< Why the last save failed; empty if it didn't.
};
} // namespace Meow2::RendererApp
