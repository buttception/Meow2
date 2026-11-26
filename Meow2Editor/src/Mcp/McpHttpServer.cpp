/// @file
/// @brief McpHttpServer (McpHttpServer.h).

#include "Mcp/McpHttpServer.h"

#include <httplib.h>

#include <format>
#include <future>
#include <optional>
#include <QMetaObject>
#include <string>

namespace Meow2::Editor
{
struct McpHttpServer::Impl
{
    httplib::Server server; ///< Listens and answers on its own threads.
};

namespace
{
constexpr std::size_t c_maxRequestBytes = std::size_t{4} * 1024 * 1024; ///< Far above any command's arguments.
constexpr auto c_pollInterval = std::chrono::milliseconds(15);          ///< About a frame, for deferred calls.

/// @brief Runs `work` on the GUI thread (a queued call to `context`) and waits for its result, giving up when the
///        server starts stopping.
template <class Work>
std::optional<McpStep> onGuiThread(QObject& context, const std::shared_ptr<std::atomic<bool>>& stopping, Work work)
{
    auto promise = std::make_shared<std::promise<McpStep>>();
    std::future<McpStep> future = promise->get_future();
    // Queued to an object on the GUI thread: it runs there at the next event-loop turn; if the object is deleted
    // first (the server is going away), Qt drops the call and the promise is never kept, which the wait below allows.
    QMetaObject::invokeMethod(&context, [promise, work] { promise->set_value(work()); }, Qt::QueuedConnection);
    while (future.wait_for(std::chrono::milliseconds(25)) != std::future_status::ready)
    {
        if (stopping->load())
        {
            return std::nullopt;
        }
    }
    return future.get();
}
} // namespace

McpHttpServer::McpHttpServer(McpProtocol& protocol, Log::Logger& log, std::chrono::milliseconds deferredTimeout)
    : m_protocol(protocol)
    , m_log(log)
    , m_deferredTimeout(deferredTimeout)
    , m_impl(std::make_unique<Impl>())
{
}

McpHttpServer::~McpHttpServer()
{
    stop();
}

McpHttpReply McpHttpServer::serve(const McpHttpRequest& request)
{
    const McpHttpReply unavailable{.status = 503, .body = {}, .headers = {}};
    std::optional<McpStep> step =
        onGuiThread(m_context, m_stopping, [this, request] { return m_protocol.handle(request); });
    const auto deadline = std::chrono::steady_clock::now() + m_deferredTimeout;
    while (step && step->pending)
    {
        // A tool waiting on the GPU (a screenshot): ask again after a frame, until it answers or time runs out.
        if (std::chrono::steady_clock::now() > deadline)
        {
            return McpProtocol::timedOut(*step->pending);
        }
        std::this_thread::sleep_for(c_pollInterval);
        const McpPending pending = *step->pending;
        step = onGuiThread(m_context, m_stopping, [this, pending] { return m_protocol.resume(pending); });
    }
    return step && step->reply ? *step->reply : unavailable;
}

Result<std::uint16_t> McpHttpServer::start(std::uint16_t port)
{
    if (running())
    {
        return m_port;
    }
    httplib::Server& server = m_impl->server;
    server.set_payload_max_length(c_maxRequestBytes);
    const auto handler = [this](const httplib::Request& request, httplib::Response& response)
    {
        McpHttpRequest mcp{.method = QByteArray::fromStdString(request.method),
                           .headers = {},
                           .body = QByteArray::fromStdString(request.body)};
        for (const auto& [name, value] : request.headers)
        {
            mcp.headers.insert(QByteArray::fromStdString(name).toLower(), QByteArray::fromStdString(value));
        }
        const McpHttpReply reply = serve(mcp);
        response.status = reply.status;
        for (const auto& [name, value] : reply.headers)
        {
            response.set_header(name.toStdString(), value.toStdString());
        }
        if (!reply.body.isEmpty())
        {
            response.set_content(reply.body.toStdString(), "application/json");
        }
    };
    // Every method goes through the protocol, which answers what isn't POST with 405 itself.
    server.Post("/mcp", handler);
    server.Get("/mcp", handler);
    server.Delete("/mcp", handler);

    // 127.0.0.1 only, never another interface: the editor answers this machine alone.
    int bound = 0;
    if (port == 0)
    {
        bound = server.bind_to_any_port("127.0.0.1");
    }
    else
    {
        bound = server.bind_to_port("127.0.0.1", port) ? port : -1;
    }
    if (bound <= 0)
    {
        return fail(ErrorCode::IoError, std::format("can't listen on 127.0.0.1:{} (in use?)", port));
    }
    m_port = static_cast<std::uint16_t>(bound);
    m_stopping->store(false);
    m_thread = std::thread([this] { m_impl->server.listen_after_bind(); });
    MEOW2_LOG_INFO(m_log, "MCP server listening on http://127.0.0.1:{}/mcp", m_port);
    return m_port;
}

void McpHttpServer::stop()
{
    if (!m_thread.joinable())
    {
        return;
    }
    // First let requests waiting on this (GUI) thread give up, then stop: httplib's stop waits for its handlers.
    m_stopping->store(true);
    m_impl->server.stop();
    m_thread.join();
    m_port = 0;
    MEOW2_LOG_INFO(m_log, "MCP server stopped");
}
} // namespace Meow2::Editor
