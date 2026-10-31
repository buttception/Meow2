/// @file
/// @brief MemTag names and the thread-local current tag (MemTagScope).

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

namespace Meow2::Memory
{
namespace
{
// GLOBAL: the current tag must be reachable from allocations that can't take a parameter (pmr containers, later
// operator new), and it's per thread by definition. techstack.md §18 lists this exception.
thread_local MemTag s_currentTag = MemTag::General;
} // namespace

MemTag currentMemTag()
{
    return s_currentTag;
}

MemTagScope::MemTagScope(MemTag tag)
    : m_previous(s_currentTag)
{
    s_currentTag = tag;
}

MemTagScope::~MemTagScope()
{
    s_currentTag = m_previous;
}
} // namespace Meow2::Memory
