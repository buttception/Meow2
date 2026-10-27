/// @file
/// @brief Windows implementation of Meow2::isDebuggerAttached().

#include "Meow2/Core/Debugger.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN // only the core API, not winsock/OLE/... (much faster to compile)
#endif
#ifndef NOMINMAX
#define NOMINMAX // windows.h must not define min/max macros (they break std::min/std::max)
#endif
#include <windows.h>

namespace Meow2
{
bool isDebuggerAttached()
{
    return IsDebuggerPresent() != FALSE;
}
} // namespace Meow2
