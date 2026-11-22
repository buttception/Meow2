/// @file
/// @brief Meow2Renderer's Debug window: the engine's debug views (Renderer::RenderDebug) as ImGui buttons, the
///        wireframe choice and frozen culling.

#pragma once

#include "UiTheme.h"

#include <Meow2/Graphics/Renderer/RenderDebug.h>

namespace Meow2::RendererApp
{
/// @brief Draws the Debug window and applies what's clicked to the debug tools' state.
/// @param debug The debug tools.
/// @param fonts The theme's fonts.
void drawDebugWindow(Renderer::RenderDebug& debug, const UiFonts& fonts = {});
} // namespace Meow2::RendererApp
