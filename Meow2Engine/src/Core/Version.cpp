/// @file
/// @brief Implementation of Meow2::getVersionString().

#include "Meow2/Core/Version.h"

namespace Meow2
{
// Compiled into the library, so it reports the library's version even when the
// caller was compiled against a different Version.h.
const char* getVersionString()
{
    return c_versionString;
}
} // namespace Meow2
