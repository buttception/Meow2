/// @file
/// @brief MemTag names.

#include "Meow2/Core/Memory/MemTag.h"

namespace Meow2::Memory
{
std::string_view memTagName(MemTag tag)
{
    switch (tag)
    {
        case MemTag::General:
            return "General";
        case MemTag::Logging:
            return "Logging";
        case MemTag::Config:
            return "Config";
        case MemTag::Jobs:
            return "Jobs";
        case MemTag::Events:
            return "Events";
        case MemTag::Platform:
            return "Platform";
        case MemTag::Renderer:
            return "Renderer";
        case MemTag::Rhi:
            return "Rhi";
        case MemTag::Physics:
            return "Physics";
        case MemTag::Assets:
            return "Assets";
        case MemTag::Editor:
            return "Editor";
        case MemTag::ImGui:
            return "ImGui";
        case MemTag::Profiler:
            return "Profiler";
        case MemTag::Count:
            break;
    }
    return "Unknown";
}
} // namespace Meow2::Memory
