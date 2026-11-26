#pragma once

/// @file
/// @brief McpProtocol: the editor's commands as MCP tools, over JSON-RPC, with no networking (McpHttpServer carries
///        it): the request checks of MCP's Streamable HTTP transport, both protocol eras, tools/list and tools/call.

#include "Commands/EditorCommandRegistry.h"

#include <Meow2/Core/Log/Logger.h>

#include <optional>
#include <QByteArray>
#include <QHash>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <utility>
#include <vector>

namespace Meow2::Editor
{
/// @brief One HTTP request to the MCP endpoint, as the transport received it. Plain data.
struct McpHttpRequest
{
    QByteArray method;                     ///< "POST", "GET", …
    QHash<QByteArray, QByteArray> headers; ///< By lower-case name.
    QByteArray body;                       ///< The JSON-RPC message.
};

/// @brief The HTTP answer to send back. Plain data.
struct McpHttpReply
{
    int status = 200;                                       ///< HTTP status.
    QByteArray body;                                        ///< JSON, or empty (202, 405).
    std::vector<std::pair<QByteArray, QByteArray>> headers; ///< Extra headers (Allow, WWW-Authenticate).
};

/// @brief A tools/call whose command is still waiting on something (the GPU): its answer comes from polling a query.
///        Plain data.
struct McpPending
{
    QJsonValue id;          ///< The JSON-RPC request's id.
    QString query;          ///< The query to run again.
    QJsonObject parameters; ///< Its parameters.
};

/// @brief What handling a request gave: an answer, or a tools/call to resume later (McpProtocol::resume()). Plain data.
struct McpStep
{
    std::optional<McpHttpReply> reply; ///< The answer, when there is one.
    std::optional<McpPending> pending; ///< Otherwise, what to poll.
};

/// @brief How the server presents itself. Plain data.
struct McpProtocolDesc
{
    QString serverName = QStringLiteral("Meow2Editor"); ///< serverInfo.name.
    QString serverVersion;                              ///< serverInfo.version: the engine's version string.
    QString instructions;                               ///< For the model: what the server is and how to use it.
    QByteArray token;                                   ///< The bearer token every request must carry.
    QStringList hidden;                                 ///< Commands not offered as tools (e.g. project.open).
};

/// @brief The protocol version of the per-request era (2026-07-28 and later): no handshake, metadata in every request.
inline constexpr const char* c_mcpModernVersion = "2026-07-28";
/// @brief The handshake-era versions served for today's clients, newest first.
inline const QStringList c_mcpLegacyVersions{QStringLiteral("2025-11-25"), QStringLiteral("2025-06-18"),
                                             QStringLiteral("2025-03-26")};

/// @brief An editor command's name as an MCP tool name: dots become underscores (`entity.create` → `entity_create`),
///        because Claude's tool names allow only letters, digits, `_` and `-` (MCP itself allows dots).
/// @param command The command's name.
/// @return The tool name.
[[nodiscard]] QString mcpToolName(const QString& command);

/// @brief The editor's commands and queries as MCP tools (techstack §20), served by McpHttpServer.
///
/// handle() takes one HTTP request and does everything but the networking:
/// - **Transport checks:** POST only (405 otherwise); an `Origin` that isn't this machine is refused (403, against DNS
///   rebinding); the bearer token (401).
/// - **Both eras** of the protocol: a request with `_meta.io.modelcontextprotocol/protocolVersion` is **modern**
///   (2026-07-28): its headers must match the body (`MCP-Protocol-Version`, `Mcp-Method`, `Mcp-Name`; 400 and
///   HeaderMismatch otherwise), an unsupported version is 400 and UnsupportedProtocolVersion with the supported list,
///   and `server/discover` describes the server. Otherwise it's **legacy** (2025-11-25 and earlier): `initialize`
///   negotiates a version, and later requests carry it in `MCP-Protocol-Version` (absent: 2025-03-26). No sessions:
///   the editor has nothing to keep per client. Notifications get 202.
/// - **Tools:** `tools/list` lists every registered command except the hidden ones, named by mcpToolName(), with its
///   description and parameter schema; queries are marked read-only. `tools/call` runs the command (on this thread:
///   the GUI thread). A failing command is a tool result with `isError` (so the model can fix its call); an unknown
///   tool is a JSON-RPC error. A result with a `png` field (base64) becomes image content beside the JSON text.
/// - **Deferred results:** a command whose answer needs a frame or two (a screenshot, a capture picture) returns
///   `{"deferred": {"query": …, "parameters": …}}`; handle() hands back an McpPending, and resume() runs that query
///   until it no longer answers `{"waiting": true}`. The transport polls between frames, never blocking the GUI.
/// @par Thread safety
/// Not thread-safe; the GUI thread (it runs commands).
class McpProtocol
{
public:
    /// @brief The protocol over a registry.
    /// @param commands The commands served as tools.
    /// @param desc     How the server presents itself, its token and hidden commands.
    /// @param log      Receives refused requests and failed calls.
    McpProtocol(EditorCommandRegistry& commands, McpProtocolDesc desc, Log::Logger& log);

    /// @brief Handles one HTTP request.
    /// @param request The request.
    /// @return The answer, or a tools/call to resume.
    [[nodiscard]] McpStep handle(const McpHttpRequest& request);

    /// @brief Runs a pending call's query again.
    /// @param pending The call.
    /// @return The answer once the query has one, or the call again.
    [[nodiscard]] McpStep resume(const McpPending& pending);

    /// @brief The answer for a pending call that ran out of time: a tool error saying so.
    /// @param pending The call.
    /// @return The answer.
    [[nodiscard]] static McpHttpReply timedOut(const McpPending& pending);

    /// @brief The tools as tools/list lists them, in a fixed order (by name).
    /// @return The tool objects.
    [[nodiscard]] std::vector<QJsonObject> tools() const;

private:
    /// @brief The command a tool name stands for, if it's offered.
    [[nodiscard]] std::optional<QString> commandFor(const QString& tool) const;

    /// @brief Runs tools/call.
    McpStep callTool(const QJsonValue& id, const QJsonObject& params, int errorStatus);

    /// @brief A command's result as a tool result (text, structured content, an image for `png`).
    [[nodiscard]] static QJsonObject toolResult(const QJsonValue& result);

    EditorCommandRegistry& m_commands;
    McpProtocolDesc m_desc;
    Log::Logger& m_log;
};
} // namespace Meow2::Editor
