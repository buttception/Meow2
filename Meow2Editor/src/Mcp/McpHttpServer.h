#pragma once

/// @file
/// @brief McpHttpServer: MCP's Streamable HTTP transport for the editor, on cpp-httplib, bound to 127.0.0.1.

#include "Mcp/McpProtocol.h"

#include <Meow2/Core/Log/Logger.h>
#include <Meow2/Core/Result.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <QObject>
#include <thread>

namespace Meow2::Editor
{
/// @brief Serves McpProtocol over HTTP at `http://127.0.0.1:<port>/mcp`.
///
/// cpp-httplib listens on a thread of its own and answers on a small pool of threads. A request is never handled
/// there: it's handed to the GUI thread (a queued call), where commands may run, and the HTTP thread waits for the
/// answer. A deferred tool call (McpPending) is polled the same way every few milliseconds, between frames, until it
/// has its answer or `deferredTimeout` passes. So the GUI thread never waits for the network, and the network threads
/// never touch editor state (techstack §20).
///
/// Shutting down can't deadlock: stop() marks the server as stopping first, so a request still waiting for the GUI
/// thread gives up with 503 instead of waiting for a thread that is busy stopping it.
/// @par Thread safety
/// Construct, start() and stop() on the GUI thread; the protocol is only ever used there.
/// @par Lifetime
/// The McpProtocol and the Logger must outlive it; destroying it stops it.
class McpHttpServer
{
public:
    /// @brief A server that isn't listening yet.
    /// @param protocol        Handles the requests (on the GUI thread).
    /// @param log             Receives the address and failures.
    /// @param deferredTimeout How long a deferred tool call may wait for its answer.
    McpHttpServer(McpProtocol& protocol, Log::Logger& log,
                  std::chrono::milliseconds deferredTimeout = std::chrono::seconds(10));

    /// @brief Stops it.
    ~McpHttpServer();

    McpHttpServer(const McpHttpServer&) = delete;            ///< Not copyable: owns a thread.
    McpHttpServer& operator=(const McpHttpServer&) = delete; ///< Not copyable.

    /// @brief Binds 127.0.0.1 (never another interface) and starts listening.
    /// @param port The port; 0 picks a free one (tests).
    /// @return The port listened on, or the error (the port is taken, …).
    [[nodiscard]] Result<std::uint16_t> start(std::uint16_t port);

    /// @brief Stops listening and waits for the server's threads.
    void stop();

    /// @brief Whether it's listening.
    /// @return True after a successful start() until stop().
    [[nodiscard]] bool running() const
    {
        return m_thread.joinable();
    }

    /// @brief The port listened on.
    /// @return It, or 0 when not running.
    [[nodiscard]] std::uint16_t port() const
    {
        return m_port;
    }

private:
    struct Impl;

    /// @brief Handles a request on the GUI thread, waiting from the HTTP thread; 503 if the server is stopping.
    [[nodiscard]] McpHttpReply serve(const McpHttpRequest& request);

    McpProtocol& m_protocol;
    Log::Logger& m_log;
    std::chrono::milliseconds m_deferredTimeout;
    std::unique_ptr<Impl> m_impl; ///< cpp-httplib, kept out of this header.
    std::shared_ptr<std::atomic<bool>> m_stopping = std::make_shared<std::atomic<bool>>(false); ///< Shared with waits.
    QObject m_context; ///< Lives on the GUI thread: queued calls to it run there; deleting it drops calls not yet run.
    std::thread m_thread;
    std::uint16_t m_port = 0;
};
} // namespace Meow2::Editor
