/// @file
/// @brief Windows CrashHandler: OS/CRT crash hooks, a watchdog thread and MiniDumpWriteDump.

#include "Meow2/Core/Assert.h"
#include "Meow2/Core/Crash/CrashHandler.h"
#include "Meow2/Core/Debugger.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
// dbghelp.h needs the types from windows.h, so it comes after it.
#include <crtdbg.h>
#include <dbghelp.h>

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <format>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

namespace Meow2::Crash
{
namespace
{
/// Private exception code raised to get a CPU context for crashes that have none (abort, terminate, …).
/// 0xE… = "error, customer-defined" range, so it can't clash with system codes.
constexpr DWORD c_contextExceptionCode = 0xE04D3243;

/// MSVC's exception code for a C++ `throw` (used to describe an uncaught exception in the log).
constexpr DWORD c_cppExceptionCode = 0xE06D7363;

/// How long the crashing thread waits for the watchdog. A hung flush (e.g. the crash happened while a
/// log sink held its lock) must not keep a dead process alive forever.
constexpr DWORD c_watchdogTimeoutMs = 10'000;

/// Extra stack reserved for the main thread's exception handling, so the handler can still run after a
/// stack overflow has used up the normal guard area.
constexpr ULONG c_stackGuaranteeBytes = 64 * 1024;

/// @brief The thread that writes the dump, plus the crash details the crashing thread hands to it.
///
/// Created up front: after a crash, creating threads or allocating memory may no longer work.
/// Not thread-safe in general: the crashing thread writes the crash fields, then sets m_crashEvent;
/// the watchdog reads them only after that event (the event is the synchronisation).
class Watchdog
{
public:
    Watchdog(std::filesystem::path dumpPath, Log::Logger& log)
        : m_dumpPath(std::move(dumpPath))
        , m_log(log)
        , m_crashEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr))
        , m_doneEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr))
        , m_shutdownEvent(CreateEventW(nullptr, TRUE, FALSE, nullptr))
    {
        std::error_code ignored;
        std::filesystem::create_directories(m_dumpPath.parent_path(), ignored);
        m_thread = std::thread([this] { run(); });
    }

    ~Watchdog()
    {
        SetEvent(m_shutdownEvent);
        if (m_thread.joinable())
        {
            m_thread.join();
        }
        CloseHandle(m_crashEvent);
        CloseHandle(m_doneEvent);
        CloseHandle(m_shutdownEvent);
    }

    Watchdog(const Watchdog&) = delete;
    Watchdog& operator=(const Watchdog&) = delete;

    /// @brief Called on the crashing thread: hands over the crash and waits for the dump (bounded).
    void handOver(EXCEPTION_POINTERS* exceptionPointers, DWORD threadId, const char* reason)
    {
        m_exceptionPointers = exceptionPointers;
        m_crashThreadId = threadId;
        m_reason = reason;
        SetEvent(m_crashEvent);
        WaitForSingleObject(m_doneEvent, c_watchdogTimeoutMs);
    }

    [[nodiscard]] const std::filesystem::path& dumpPath() const
    {
        return m_dumpPath;
    }

private:
    void run()
    {
        SetThreadDescription(GetCurrentThread(), L"Meow2 crash watchdog");

        const HANDLE events[] = {m_shutdownEvent, m_crashEvent};
        const DWORD signalled = WaitForMultipleObjects(2, events, FALSE, INFINITE);
        if (signalled != WAIT_OBJECT_0 + 1)
        {
            return; // shutdown (or an error waiting): no crash happened
        }

        // Dump first, while nothing else has run: logging allocates and may fail with a corrupt heap.
        const bool written = writeDump();
        report(written);
        SetEvent(m_doneEvent);
    }

    [[nodiscard]] bool writeDump() const
    {
        const HANDLE file =
            CreateFileW(m_dumpPath.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE)
        {
            return false;
        }

        MINIDUMP_EXCEPTION_INFORMATION exceptionInfo{};
        exceptionInfo.ThreadId = m_crashThreadId;
        exceptionInfo.ExceptionPointers = m_exceptionPointers;
        exceptionInfo.ClientPointers = FALSE; // the pointers are valid in this process

        // Stacks and registers of all threads, the memory they point to (enough to inspect locals), thread
        // and handle details, and recently unloaded modules. Typically a few MB, not the whole heap.
        const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo |
                                                     MiniDumpWithHandleData | MiniDumpWithUnloadedModules);
        const BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                                          m_exceptionPointers != nullptr ? &exceptionInfo : nullptr, nullptr, nullptr);
        CloseHandle(file);
        return ok != FALSE;
    }

    void report(bool written) const
    {
        std::string what;
        if (m_reason != nullptr)
        {
            what = m_reason;
        }
        else
        {
            const DWORD code = m_exceptionPointers->ExceptionRecord->ExceptionCode;
            what = code == c_cppExceptionCode
                       ? std::string{"uncaught C++ exception"}
                       : std::format("unhandled exception 0x{:08X}", static_cast<unsigned>(code));
        }
        const std::string path = m_dumpPath.string();

        if (written)
        {
            std::fprintf(stderr, "Meow2: crash (%s); dump written to %s\n", what.c_str(), path.c_str());
            m_log.error("Crash ({}): dump written to {}", what, path);
        }
        else
        {
            const DWORD error = GetLastError();
            std::fprintf(stderr, "Meow2: crash (%s); the dump couldn't be written to %s\n", what.c_str(), path.c_str());
            m_log.error("Crash ({}): the dump couldn't be written to {} (error {})", what, path, error);
        }
        std::fflush(stderr);
        m_log.flush();
    }

    std::filesystem::path m_dumpPath;
    Log::Logger& m_log;
    HANDLE m_crashEvent;
    HANDLE m_doneEvent;
    HANDLE m_shutdownEvent;
    std::thread m_thread;

    // Written by the crashing thread before m_crashEvent is set.
    EXCEPTION_POINTERS* m_exceptionPointers = nullptr;
    DWORD m_crashThreadId = 0;
    const char* m_reason = nullptr; // nullptr: a structured exception, described by its code
};

// GLOBAL: the OS and CRT crash callbacks have no user-data parameter, so they reach the installed
// handler's watchdog through this pointer (techstack.md §18). Set and cleared by CrashHandler only.
std::atomic<Watchdog*> s_activeWatchdog{nullptr};

// GLOBAL: id of the thread that is handling a crash (0 = none), so a crash on a second thread waits
// instead of writing another dump, and a re-entry on the same thread (terminate → abort) returns.
std::atomic<DWORD> s_crashingThreadId{0};

/// @brief Hands a crash to the watchdog and waits until the dump is written (or the timeout passes).
/// Runs on the crashing thread, possibly with almost no stack left: no allocations, no C++ objects.
/// @return `true` if a dump was requested; `false` if no handler is installed, a debugger is attached,
///         or this thread is already handling a crash.
bool handleCrash(EXCEPTION_POINTERS* exceptionPointers, const char* reason)
{
    Watchdog* const watchdog = s_activeWatchdog.load();
    if (watchdog == nullptr || isDebuggerAttached())
    {
        return false;
    }

    const DWORD thisThread = GetCurrentThreadId();
    DWORD noThread = 0;
    if (!s_crashingThreadId.compare_exchange_strong(noThread, thisThread))
    {
        if (noThread == thisThread)
        {
            return false; // re-entered on the same thread (e.g. terminate handler → abort): already handled
        }
        Sleep(INFINITE); // another thread is writing the dump and will end the process
    }

    watchdog->handOver(exceptionPointers, thisThread, reason);
    return true;
}

/// @brief For crashes without an exception context: raises a private structured exception, so the dump
///        contains this thread's registers and the debugger opens at the crash site.
/// Contains no C++ objects with destructors: `__try` isn't allowed in functions that need C++ unwinding.
bool handleCrashWithContext(const char* reason)
{
    bool handled = false;
    __try
    {
        RaiseException(c_contextExceptionCode, EXCEPTION_NONCONTINUABLE, 0, nullptr);
    }
    __except (handled = handleCrash(GetExceptionInformation(), reason), EXCEPTION_EXECUTE_HANDLER)
    {
    }
    return handled;
}

/// @brief After a crash that wasn't dumped because a debugger is attached, stop in the debugger first.
void breakIfDebugging()
{
    if (isDebuggerAttached())
    {
        MEOW2_DEBUG_BREAK();
    }
}

LONG WINAPI onUnhandledException(EXCEPTION_POINTERS* exceptionPointers)
{
    // EXECUTE_HANDLER ends the process with the exception code (no Windows Error Reporting dialog);
    // CONTINUE_SEARCH hands the exception on (e.g. to an attached debugger).
    return handleCrash(exceptionPointers, nullptr) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH;
}

void __cdecl onAbortSignal(int /*signal*/)
{
    if (!handleCrashWithContext("abort()"))
    {
        breakIfDebugging();
    }
    // Returning lets abort() finish: the process ends with exit code 3.
}

[[noreturn]] void onTerminate()
{
    if (!handleCrashWithContext("std::terminate (e.g. an uncaught C++ exception)"))
    {
        breakIfDebugging();
    }
    std::_Exit(3); // not abort(): that would only re-enter the handler
}

void __cdecl onPureCall()
{
    if (!handleCrashWithContext("pure virtual function call"))
    {
        breakIfDebugging();
    }
    std::_Exit(3);
}

void __cdecl onInvalidParameter(const wchar_t* /*expression*/, const wchar_t* /*function*/, const wchar_t* /*file*/,
                                unsigned int /*line*/, std::uintptr_t /*reserved*/)
{
    // The arguments are only filled in by the Debug CRT; the dump has the full call stack anyway.
    if (!handleCrashWithContext("CRT invalid parameter"))
    {
        breakIfDebugging();
    }
    std::_Exit(3);
}
} // namespace

/// @brief Owns the watchdog and remembers every handler that was installed before, to restore them.
struct CrashHandler::Impl
{
    Impl(const CrashHandlerDesc& desc, Log::Logger& log)
        : m_watchdog(desc.dumpPath, log)
    {
        Watchdog* noWatchdog = nullptr;
        const bool installed = s_activeWatchdog.compare_exchange_strong(noWatchdog, &m_watchdog);
        MEOW2_ASSERT(installed, "only one CrashHandler may exist at a time");

        m_previousFilter = SetUnhandledExceptionFilter(&onUnhandledException);
        m_previousAbortSignal = std::signal(SIGABRT, &onAbortSignal);
        m_previousTerminate = std::set_terminate(&onTerminate);
        m_previousPureCall = _set_purecall_handler(&onPureCall);
        m_previousInvalidParameter = _set_invalid_parameter_handler(&onInvalidParameter);

        // No "abort() has been called" message box and no Windows Error Reporting for abort(): the dump is
        // the report.
        m_previousAbortBehavior = _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);

        if (!isDebuggerAttached())
        {
            // Debug CRT checks normally open a dialog that blocks an unattended program. Print them to stderr
            // instead; the hooks above then write the dump. (These calls do nothing with the Release CRT.)
            m_previousAssertReportMode = _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
            m_previousAssertReportFile = _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
            m_previousErrorReportMode = _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
            m_previousErrorReportFile = _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
            m_changedReportModes = true;
        }

        ULONG guarantee = c_stackGuaranteeBytes;
        SetThreadStackGuarantee(&guarantee);
    }

    ~Impl()
    {
        if (m_changedReportModes)
        {
            _CrtSetReportMode(_CRT_ASSERT, m_previousAssertReportMode);
            _CrtSetReportFile(_CRT_ASSERT, m_previousAssertReportFile);
            _CrtSetReportMode(_CRT_ERROR, m_previousErrorReportMode);
            _CrtSetReportFile(_CRT_ERROR, m_previousErrorReportFile);
        }
        _set_abort_behavior(m_previousAbortBehavior, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
        _set_invalid_parameter_handler(m_previousInvalidParameter);
        _set_purecall_handler(m_previousPureCall);
        std::set_terminate(m_previousTerminate);
        std::signal(SIGABRT, m_previousAbortSignal);
        SetUnhandledExceptionFilter(m_previousFilter);

        Watchdog* self = &m_watchdog;
        s_activeWatchdog.compare_exchange_strong(self, nullptr);
    }

    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;

    Watchdog m_watchdog;
    LPTOP_LEVEL_EXCEPTION_FILTER m_previousFilter = nullptr;
    _crt_signal_t m_previousAbortSignal = SIG_DFL;
    std::terminate_handler m_previousTerminate = nullptr;
    _purecall_handler m_previousPureCall = nullptr;
    _invalid_parameter_handler m_previousInvalidParameter = nullptr;
    unsigned int m_previousAbortBehavior = 0;
    int m_previousAssertReportMode = 0;
    int m_previousErrorReportMode = 0;
    _HFILE m_previousAssertReportFile = nullptr;
    _HFILE m_previousErrorReportFile = nullptr;
    bool m_changedReportModes = false;
};

CrashHandler::CrashHandler(const CrashHandlerDesc& desc, Log::Logger& log)
    : m_impl(std::make_unique<Impl>(desc, log))
{
}

CrashHandler::~CrashHandler() = default;

bool CrashHandler::isActive() const
{
    return true;
}

const std::filesystem::path& CrashHandler::dumpPath() const
{
    return m_impl->m_watchdog.dumpPath();
}
} // namespace Meow2::Crash
