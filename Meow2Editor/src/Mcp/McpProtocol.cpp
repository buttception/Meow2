/// @file
/// @brief McpProtocol (McpProtocol.h).

#include "Mcp/McpProtocol.h"

#include <algorithm>
#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>

namespace Meow2::Editor
{
using namespace Qt::StringLiterals;

namespace
{
// JSON-RPC 2.0's codes, and the ones MCP defines (2026-07-28, "Error Codes").
constexpr int c_parseError = -32700;
constexpr int c_invalidRequest = -32600;
constexpr int c_methodNotFound = -32601;
constexpr int c_invalidParams = -32602;
constexpr int c_headerMismatch = -32020;
constexpr int c_unsupportedVersion = -32022;

const QString c_versionKey = u"io.modelcontextprotocol/protocolVersion"_s;
const QString c_capabilitiesKey = u"io.modelcontextprotocol/clientCapabilities"_s;

QByteArray toJson(const QJsonObject& object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

McpHttpReply jsonReply(int status, const QJsonObject& body)
{
    return McpHttpReply{.status = status, .body = toJson(body), .headers = {}};
}

McpHttpReply resultReply(const QJsonValue& id, QJsonObject result)
{
    if (!result.contains(u"resultType"))
    {
        result.insert(u"resultType", u"complete"_s); // modern clients need it; older ones ignore it
    }
    return jsonReply(200, QJsonObject{{u"jsonrpc"_s, u"2.0"_s}, {u"id"_s, id}, {u"result"_s, result}});
}

McpHttpReply errorReply(int status, const QJsonValue& id, int code, const QString& message,
                        const QJsonValue& data = QJsonValue::Undefined)
{
    QJsonObject error{{u"code"_s, code}, {u"message"_s, message}};
    if (!data.isUndefined())
    {
        error.insert(u"data", data);
    }
    QJsonObject body{{u"jsonrpc"_s, u"2.0"_s}, {u"error"_s, error}};
    if (!id.isUndefined() && !id.isNull())
    {
        body.insert(u"id", id);
    }
    return jsonReply(status, body);
}

McpStep step(McpHttpReply reply)
{
    return McpStep{.reply = std::move(reply), .pending = std::nullopt};
}

/// @brief Compares the token without stopping at the first difference, so timing says nothing about it.
bool sameToken(const QByteArray& given, const QByteArray& expected)
{
    if (given.size() != expected.size() || expected.isEmpty())
    {
        return false;
    }
    unsigned char difference = 0;
    for (qsizetype i = 0; i < given.size(); ++i)
    {
        difference |= static_cast<unsigned char>(given[i] ^ expected[i]);
    }
    return difference == 0;
}

/// @brief Whether an Origin header names this machine. Browsers send one; a page elsewhere that rebinds a DNS name to
///        127.0.0.1 still sends its own origin, which is refused.
bool localOrigin(const QByteArray& origin)
{
    const QString host = QUrl(QString::fromLatin1(origin)).host().toLower();
    return host == u"127.0.0.1"_s || host == u"localhost"_s || host == u"::1"_s;
}

/// @brief A header value as MCP encodes it: plain, or `=?base64?…?=` for values that aren't plain ASCII.
QString decodedHeader(const QByteArray& value)
{
    if (value.startsWith("=?base64?") && value.endsWith("?="))
    {
        return QString::fromUtf8(QByteArray::fromBase64(value.mid(9, value.size() - 11)));
    }
    return QString::fromLatin1(value);
}

QJsonArray supportedVersions()
{
    QJsonArray versions{QString::fromLatin1(c_mcpModernVersion)};
    for (const QString& version : c_mcpLegacyVersions)
    {
        versions.append(version);
    }
    return versions;
}
} // namespace

QString mcpToolName(const QString& command)
{
    QString name = command;
    name.replace(u'.', u'_');
    return name;
}

McpProtocol::McpProtocol(EditorCommandRegistry& commands, McpProtocolDesc desc, Log::Logger& log)
    : m_commands(commands)
    , m_desc(std::move(desc))
    , m_log(log)
{
}

std::vector<QJsonObject> McpProtocol::tools() const
{
    std::vector<CommandInfo> commands = m_commands.commands();
    std::ranges::sort(commands, {}, &CommandInfo::name); // the same order every time: clients cache the list
    std::vector<QJsonObject> tools;
    for (const CommandInfo& command : commands)
    {
        if (m_desc.hidden.contains(command.name))
        {
            continue;
        }
        // No parameters: an object with nothing in it, which MCP recommends spelling out.
        const QJsonObject schema = command.parameters.isEmpty()
                                       ? QJsonObject{{u"type"_s, u"object"_s}, {u"additionalProperties"_s, false}}
                                       : command.parameters;
        const bool query = command.kind == CommandKind::Query;
        tools.push_back(QJsonObject{
            {u"name"_s, mcpToolName(command.name)},
            {u"title"_s, command.name},
            {u"description"_s, command.description},
            {u"inputSchema"_s, schema},
            {u"annotations"_s,
             QJsonObject{{u"readOnlyHint"_s, query}, {u"destructiveHint"_s, false}, {u"openWorldHint"_s, false}}}});
    }
    return tools;
}

std::optional<QString> McpProtocol::commandFor(const QString& tool) const
{
    for (const CommandInfo& command : m_commands.commands())
    {
        if (mcpToolName(command.name) == tool && !m_desc.hidden.contains(command.name))
        {
            return command.name;
        }
    }
    return std::nullopt;
}

McpStep McpProtocol::handle(const McpHttpRequest& request)
{
    // Transport checks first: nothing reaches the commands without them.
    if (request.method != "POST")
    {
        // 2026-07-28 has no GET stream and no sessions to DELETE.
        return step(McpHttpReply{.status = 405, .body = {}, .headers = {{"Allow", "POST"}}});
    }
    if (const auto origin = request.headers.find("origin"); origin != request.headers.end() && !localOrigin(*origin))
    {
        MEOW2_LOG_WARN(m_log, "MCP: refused a request from origin {}", origin->toStdString());
        return step(errorReply(403, QJsonValue::Null, c_invalidRequest, u"Origin not allowed"_s));
    }
    if (!sameToken(request.headers.value("authorization"), "Bearer " + m_desc.token))
    {
        McpHttpReply reply = errorReply(401, QJsonValue::Null, c_invalidRequest, u"Missing or wrong bearer token"_s);
        reply.headers.emplace_back("WWW-Authenticate", "Bearer");
        return step(std::move(reply));
    }

    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(request.body, &parseError);
    if (parseError.error != QJsonParseError::NoError)
    {
        return step(errorReply(400, QJsonValue::Null, c_parseError, u"Parse error: "_s + parseError.errorString()));
    }
    const QJsonObject message = document.object();
    if (!document.isObject() || message.value(u"jsonrpc") != u"2.0"_s || !message.value(u"method").isString())
    {
        return step(errorReply(400, message.value(u"id"), c_invalidRequest, u"Not a JSON-RPC 2.0 request"_s));
    }
    const QString method = message.value(u"method").toString();
    const QJsonObject params = message.value(u"params").toObject();
    if (!message.contains(u"id"))
    {
        return step(McpHttpReply{.status = 202, .body = {}, .headers = {}}); // notifications: nothing to answer
    }
    const QJsonValue id = message.value(u"id");

    // Which era: a modern request says its version in _meta; a legacy one starts with initialize, then puts the
    // negotiated version in the MCP-Protocol-Version header.
    const QJsonObject meta = params.value(u"_meta").toObject();
    const bool modern = meta.contains(c_versionKey);
    const int notFoundStatus = modern ? 404 : 200;
    if (modern)
    {
        const QString version = meta.value(c_versionKey).toString();
        if (version != QString::fromLatin1(c_mcpModernVersion) && !c_mcpLegacyVersions.contains(version))
        {
            return step(errorReply(400, id, c_unsupportedVersion, u"Unsupported protocol version"_s,
                                   QJsonObject{{u"supported"_s, supportedVersions()}, {u"requested"_s, version}}));
        }
        // The headers mirror the body for intermediaries; they must agree with it.
        const auto mismatch = [&](const QString& what)
        {
            return step(errorReply(400, id, c_headerMismatch, u"Header mismatch: "_s + what));
        };
        if (decodedHeader(request.headers.value("mcp-protocol-version")) != version)
        {
            return mismatch(u"MCP-Protocol-Version"_s);
        }
        if (decodedHeader(request.headers.value("mcp-method")) != method)
        {
            return mismatch(u"Mcp-Method"_s);
        }
        if (method == u"tools/call" &&
            decodedHeader(request.headers.value("mcp-name")) != params.value(u"name").toString())
        {
            return mismatch(u"Mcp-Name"_s);
        }
        if (!meta.value(c_capabilitiesKey).isObject())
        {
            return step(errorReply(400, id, c_invalidParams, u"_meta lacks "_s + c_capabilitiesKey));
        }
    }
    else if (method != u"initialize")
    {
        // Legacy: the version negotiated by initialize, in the header since 2025-06-18; none means 2025-03-26.
        const QByteArray header = request.headers.value("mcp-protocol-version");
        const QString version = header.isEmpty() ? u"2025-03-26"_s : QString::fromLatin1(header);
        if (!c_mcpLegacyVersions.contains(version) && version != QString::fromLatin1(c_mcpModernVersion))
        {
            return step(errorReply(400, id, c_unsupportedVersion, u"Unsupported protocol version"_s,
                                   QJsonObject{{u"supported"_s, supportedVersions()}, {u"requested"_s, version}}));
        }
    }

    const QJsonObject serverInfo{{u"name"_s, m_desc.serverName}, {u"version"_s, m_desc.serverVersion}};
    if (method == u"initialize" && !modern)
    {
        const QString requested = params.value(u"protocolVersion").toString();
        const QString version = c_mcpLegacyVersions.contains(requested) ? requested : c_mcpLegacyVersions.front();
        return step(resultReply(
            id, QJsonObject{{u"protocolVersion"_s, version},
                            {u"capabilities"_s, QJsonObject{{u"tools"_s, QJsonObject{{u"listChanged"_s, false}}}}},
                            {u"serverInfo"_s, serverInfo},
                            {u"instructions"_s, m_desc.instructions}}));
    }
    if (method == u"server/discover")
    {
        return step(resultReply(
            id, QJsonObject{{u"supportedVersions"_s, supportedVersions()},
                            {u"capabilities"_s, QJsonObject{{u"tools"_s, QJsonObject{{u"listChanged"_s, false}}}}},
                            {u"instructions"_s, m_desc.instructions},
                            {u"ttlMs"_s, 0},
                            {u"cacheScope"_s, u"private"_s},
                            {u"_meta"_s, QJsonObject{{u"io.modelcontextprotocol/serverInfo"_s, serverInfo}}}}));
    }
    if (method == u"ping")
    {
        return step(resultReply(id, {}));
    }
    if (method == u"tools/list")
    {
        QJsonArray tools;
        for (const QJsonObject& tool : this->tools())
        {
            tools.append(tool);
        }
        return step(resultReply(id, QJsonObject{{u"tools"_s, tools}}));
    }
    if (method == u"tools/call")
    {
        return callTool(id, params, modern ? 400 : 200);
    }
    return step(errorReply(notFoundStatus, id, c_methodNotFound, u"Method not found: "_s + method));
}

McpStep McpProtocol::callTool(const QJsonValue& id, const QJsonObject& params, int errorStatus)
{
    if (!params.value(u"name").isString() || (params.contains(u"arguments") && !params.value(u"arguments").isObject()))
    {
        return step(errorReply(errorStatus, id, c_invalidParams, u"tools/call needs a name and object arguments"_s));
    }
    const QString tool = params.value(u"name").toString();
    const std::optional<QString> command = commandFor(tool);
    if (!command)
    {
        return step(errorReply(errorStatus, id, c_invalidParams, u"Unknown tool: "_s + tool));
    }
    const Result<QJsonValue> ran = m_commands.execute(*command, params.value(u"arguments").toObject());
    if (!ran)
    {
        // A tool execution error, not a protocol one: the model reads it and can correct its call.
        MEOW2_LOG_INFO(m_log, "MCP: {} failed: {}", command->toStdString(), ran.error());
        return step(resultReply(
            id, QJsonObject{
                    {u"content"_s, QJsonArray{QJsonObject{{u"type"_s, u"text"_s},
                                                          {u"text"_s, QString::fromStdString(ran.error().message)}}}},
                    {u"isError"_s, true}}));
    }
    if (const QJsonObject deferred = ran->toObject().value(u"deferred").toObject(); !deferred.isEmpty())
    {
        return McpStep{.reply = std::nullopt,
                       .pending = McpPending{.id = id,
                                             .query = deferred.value(u"query").toString(),
                                             .parameters = deferred.value(u"parameters").toObject()}};
    }
    return step(resultReply(id, toolResult(*ran)));
}

McpStep McpProtocol::resume(const McpPending& pending)
{
    const Result<QJsonValue> ran = m_commands.execute(pending.query, pending.parameters);
    if (!ran)
    {
        return step(resultReply(
            pending.id, QJsonObject{{u"content"_s,
                                     QJsonArray{QJsonObject{{u"type"_s, u"text"_s},
                                                            {u"text"_s, QString::fromStdString(ran.error().message)}}}},
                                    {u"isError"_s, true}}));
    }
    if (ran->toObject().value(u"waiting").toBool())
    {
        return McpStep{.reply = std::nullopt, .pending = pending};
    }
    return step(resultReply(pending.id, toolResult(*ran)));
}

McpHttpReply McpProtocol::timedOut(const McpPending& pending)
{
    return resultReply(
        pending.id,
        QJsonObject{{u"content"_s, QJsonArray{QJsonObject{{u"type"_s, u"text"_s},
                                                          {u"text"_s, u"Timed out waiting for "_s + pending.query +
                                                                          u" (is the editor drawing frames?)"_s}}}},
                    {u"isError"_s, true}});
}

QJsonObject McpProtocol::toolResult(const QJsonValue& result)
{
    QJsonArray content;
    QJsonObject output;
    if (result.isObject())
    {
        QJsonObject object = result.toObject();
        // A picture (base64 PNG) is shown to the model as an image, not as a wall of base64 in the text.
        if (const QJsonValue png = object.value(u"png"); png.isString())
        {
            content.append(QJsonObject{{u"type"_s, u"image"_s}, {u"data"_s, png}, {u"mimeType"_s, u"image/png"_s}});
            object.remove(u"png");
        }
        content.prepend(QJsonObject{{u"type"_s, u"text"_s}, {u"text"_s, QString::fromUtf8(toJson(object))}});
        output.insert(u"structuredContent", object);
    }
    else if (result.isUndefined() || result.isNull())
    {
        content.append(QJsonObject{{u"type"_s, u"text"_s}, {u"text"_s, u"OK"_s}});
    }
    else
    {
        // An array or a plain value: as JSON text (structured content is kept to objects, which every client takes).
        const QJsonDocument document = result.isArray() ? QJsonDocument(result.toArray()) : QJsonDocument();
        content.append(
            QJsonObject{{u"type"_s, u"text"_s},
                        {u"text"_s, result.isArray() ? QString::fromUtf8(document.toJson(QJsonDocument::Compact))
                                                     : result.toVariant().toString()}});
    }
    output.insert(u"content", content);
    output.insert(u"isError", false);
    return output;
}
} // namespace Meow2::Editor
