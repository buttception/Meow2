/// @file
/// @brief Assert failure reporting: handler storage, the default handler and the recursion guard.

#include "Meow2/Core/Assert.h"

#include "Meow2/Core/Debugger.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>

namespace Meow2
{
namespace
{
// GLOBAL: asserts are macros usable from any code, including code with no access to the
// composition root, so the handler they report to is process-wide. It is set only by the
// composition root and by tests; atomic, because asserts can fail on any thread.
std::atomic<AssertHandler> s_assertHandler{&defaultAssertHandler};

// GLOBAL: per-thread recursion guard. True while this thread is inside a handler, so an
// assert failing inside the handler aborts instead of recursing until the stack overflows.
thread_local bool s_isReportingAssert = false;
} // namespace

AssertAction defaultAssertHandler(const AssertInfo& info)
{
    // stderr and fprintf only: this may run with a corrupted heap, so nothing that allocates.
    std::fprintf(stderr, "Assertion failed: %s\n", info.expression);
    if (info.message[0] != '\0')
    {
        std::fprintf(stderr, "  Message: %s\n", info.message);
    }
    // "file(line)" is the format Visual Studio's output window and most editors make clickable.
    std::fprintf(stderr, "  At: %s(%d) in %s\n", info.file, info.line, info.function);
    std::fflush(stderr);

    return isDebuggerAttached() ? AssertAction::Break : AssertAction::Abort;
}

AssertHandler setAssertHandler(AssertHandler handler)
{
    return s_assertHandler.exchange(handler != nullptr ? handler : &defaultAssertHandler);
}

namespace Detail
{
bool reportAssertFailure(const AssertInfo& info)
{
    if (s_isReportingAssert)
    {
        std::fputs("Assertion failed inside an assert handler; aborting.\n", stderr);
        std::fflush(stderr);
        std::abort();
    }

    s_isReportingAssert = true;
    const AssertAction action = s_assertHandler.load()(info);
    s_isReportingAssert = false;

    switch (action)
    {
        case AssertAction::Break:
            return true;
        case AssertAction::Continue:
            return false;
        case AssertAction::Abort:
            break;
    }
    std::abort();
}
} // namespace Detail
} // namespace Meow2
