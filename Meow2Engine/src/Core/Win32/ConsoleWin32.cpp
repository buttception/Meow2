/// @file
/// @brief enableUtf8ConsoleOutput() on Windows.

#include "Core/Console.h"

#include <windows.h>

namespace Meow2
{
void enableUtf8ConsoleOutput()
{
    // Without a console (Release GUI-subsystem builds) this fails harmlessly; nothing to report.
    static_cast<void>(SetConsoleOutputCP(CP_UTF8));
}
} // namespace Meow2
