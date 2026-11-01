/// @file
/// @brief Implementation of Meow2::getVersionString() and Meow2::getBuildMetadata().

#include "Meow2/Core/Version.h"

// Written at build time by cmake/GitVersion.cmake (the meow2_git_version target), into the build tree.
#include "Core/BuildMetadata.h"

namespace Meow2
{
// Compiled into the library, so it reports the library's version even when the
// caller was compiled against a different Version.h.
const char* getVersionString()
{
    return Detail::c_fullVersionString;
}

const char* getBuildMetadata()
{
    return Detail::c_buildMetadata;
}
} // namespace Meow2
