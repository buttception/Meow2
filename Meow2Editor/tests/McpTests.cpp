/// @file
/// @brief Tests for the MCP server: the command-line options, the protocol (both eras, the request checks, tools,
///        errors, images, deferred results), the HTTP transport, and a headless editor driven over MCP.

#include "Commands/CommandSchema.h"
#include "EditorTestRig.h"
#include "Mcp/EditorArguments.h"
#include "Mcp/McpHttpServer.h"
#include "Mcp/McpProtocol.h"

#include <httplib.h>

#include <array>
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <future>
#include <iterator>
#include <optional>
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <string>
#include <thread>
#include <vector>

namespace
{
using namespace Qt::StringLiterals;
namespace Editor = Meow2::Editor;

constexpr const char* c_token = "test-token-0123456789abcdef";

/// @brief The protocol over the rig's registry, plus test commands: an echo query, a failing action, a picture and a
///        deferred one that's ready on the third poll.
class McpProtocolTest : public Meow2::Tests::EditorTest
{
protected:
    McpProtocolTest()
    {
        const auto add = [this](Editor::CommandInfo info, Editor::CommandHandler handler)
        {
            EXPECT_TRUE(m_commands.add(std::move(info), std::move(handler)));
        };
        add({.name = u"test.echo"_s,
             .description = u"Echoes."_s,
             .kind = Editor::CommandKind::Query,
             .parameters =
                 Editor::objectSchema({{.name = u"a"_s, .schema = Editor::propertySchema(u"integer"_s, u"A."_s)},
                                       {.name = u"x"_s, .schema = Editor::propertySchema(u"string"_s, u"X."_s)}})},
            [](const QJsonObject& parameters) -> Meow2::Result<Editor::CommandOutcome>
            { return Editor::CommandOutcome{.result = parameters}; });
        add({.name = u"test.fail"_s, .description = u"Fails."_s},
            [](const QJsonObject&) -> Meow2::Result<Editor::CommandOutcome>
            { return Meow2::fail(Meow2::ErrorCode::InvalidArgument, "it always fails"); });
        add({.name = u"test.picture"_s, .description = u"A picture."_s, .kind = Editor::CommandKind::Query},
            [](const QJsonObject&) -> Meow2::Result<Editor::CommandOutcome>
            { return Editor::CommandOutcome{.result = QJsonObject{{u"width"_s, 1}, {u"png"_s, u"iVBORw0K"_s}}}; });
        add({.name = u"test.later"_s, .description = u"Answers later."_s},
            [](const QJsonObject&) -> Meow2::Result<Editor::CommandOutcome>
            {
                return Editor::CommandOutcome{
                    .result = QJsonObject{{u"deferred"_s, QJsonObject{{u"query"_s, u"test.ready"_s},
                                                                      {u"parameters"_s, QJsonObject{{u"n"_s, 7}}}}}}};
            });
        add({.name = u"test.ready"_s,
             .description = u"Ready on the third call."_s,
             .kind = Editor::CommandKind::Query,
             .parameters = Editor::objectSchema(
                 {{.name = u"n"_s, .schema = Editor::propertySchema(u"integer"_s, u"N."_s), .required = true}})},
            [this](const QJsonObject& parameters) -> Meow2::Result<Editor::CommandOutcome>
            {
                if (++m_polls < 3)
                {
                    return Editor::CommandOutcome{.result = QJsonObject{{u"waiting"_s, true}}};
                }
                return Editor::CommandOutcome{.result = QJsonObject{{u"n"_s, parameters.value(u"n")}}};
            });
    }

    /// @brief A POST with the token and a body.
    static Editor::McpHttpRequest post(const QJsonObject& body, QHash<QByteArray, QByteArray> headers = {})
    {
        headers.insert("authorization", QByteArray("Bearer ") + c_token);
        return {.method = "POST", .headers = headers, .body = QJsonDocument(body).toJson(QJsonDocument::Compact)};
    }

    /// @brief A JSON-RPC request.
    static QJsonObject request(int id, const QString& method, const QJsonObject& params = {})
    {
        return {{u"jsonrpc"_s, u"2.0"_s}, {u"id"_s, id}, {u"method"_s, method}, {u"params"_s, params}};
    }

    /// @brief The headers and _meta of a modern request.
    static std::pair<QJsonObject, QHash<QByteArray, QByteArray>> modern(int id, const QString& method,
                                                                        QJsonObject params = {})
    {
        params.insert(u"_meta", QJsonObject{{u"io.modelcontextprotocol/protocolVersion"_s, u"2026-07-28"_s},
                                            {u"io.modelcontextprotocol/clientCapabilities"_s, QJsonObject{}}});
        QHash<QByteArray, QByteArray> headers{{"mcp-protocol-version", "2026-07-28"}, {"mcp-method", method.toUtf8()}};
        if (method == u"tools/call")
        {
            headers.insert("mcp-name", params.value(u"name").toString().toUtf8());
        }
        return {request(id, method, params), headers};
    }

    /// @brief Handles a request that must be answered at once.
    Editor::McpHttpReply send(const Editor::McpHttpRequest& request)
    {
        Editor::McpStep step = m_protocol.handle(request);
        EXPECT_TRUE(step.reply.has_value()) << "answered at once";
        return step.reply.value_or(Editor::McpHttpReply{.status = 0, .body = {}, .headers = {}});
    }

    /// @brief A reply's JSON body.
    static QJsonObject body(const Editor::McpHttpReply& reply)
    {
        return QJsonDocument::fromJson(reply.body).object();
    }

    int m_polls = 0;
    Editor::McpProtocol m_protocol{m_commands,
                                   {.serverName = u"Meow2Editor"_s,
                                    .serverVersion = u"0.8.0"_s,
                                    .instructions = u"Test."_s,
                                    .token = c_token,
                                    .hidden = {u"test.echo.hidden"_s, u"settings.set"_s}},
                                   m_log};
};

using EditorArgumentsTest = ::testing::Test;
using McpHttpTest = McpProtocolTest;
using McpHeadlessTest = Meow2::Tests::QtTest;
} // namespace

TEST(EditorArgumentsTest, ReadsOptions)
{
    const std::array<const char*, 9> line{"Meow2Editor", "--set", "a=1",         "--headless",       "--mcp",
                                          "--mcp-port",  "4567",  "--mcp-token", "abcdefghijklmnopq"};
    const Meow2::Result<Editor::EditorArguments> parsed =
        Editor::parseEditorArguments(static_cast<int>(line.size()), line.data());
    ASSERT_TRUE(parsed) << parsed.error().message;
    EXPECT_TRUE(parsed->headless);
    EXPECT_TRUE(parsed->mcp);
    EXPECT_EQ(parsed->mcpPort, 4567);
    EXPECT_EQ(parsed->mcpToken, "abcdefghijklmnopq");

    const std::array<const char*, 1> none{"Meow2Editor"};
    const Meow2::Result<Editor::EditorArguments> nothing = Editor::parseEditorArguments(1, none.data());
    ASSERT_TRUE(nothing);
    EXPECT_FALSE(nothing->headless || nothing->mcp || nothing->mcpPort || nothing->mcpToken);

    for (const std::vector<const char*>& bad : std::vector<std::vector<const char*>>{{"e", "--mcp-port"},
                                                                                     {"e", "--mcp-port", "0"},
                                                                                     {"e", "--mcp-port", "70000"},
                                                                                     {"e", "--mcp-port", "8x"},
                                                                                     {"e", "--mcp-token", "short"}})
    {
        EXPECT_FALSE(Editor::parseEditorArguments(static_cast<int>(bad.size()), bad.data())) << bad.back();
    }
}

// Today's clients: initialize, then requests with the negotiated version in a header.
TEST_F(McpProtocolTest, LegacyHandshakeAndTools)
{
    const Editor::McpHttpReply init =
        send(post(request(1, u"initialize"_s,
                          {{u"protocolVersion"_s, u"2025-06-18"_s},
                           {u"capabilities"_s, QJsonObject{}},
                           {u"clientInfo"_s, QJsonObject{{u"name"_s, u"t"_s}, {u"version"_s, u"1"_s}}}})));
    ASSERT_EQ(init.status, 200);
    const QJsonObject result = body(init).value(u"result").toObject();
    EXPECT_EQ(result.value(u"protocolVersion").toString(), u"2025-06-18");
    EXPECT_EQ(result.value(u"serverInfo").toObject().value(u"version").toString(), u"0.8.0");
    EXPECT_TRUE(result.value(u"capabilities").toObject().contains(u"tools"));

    // An unknown version gets the newest legacy one.
    const Editor::McpHttpReply older =
        send(post(request(2, u"initialize"_s, {{u"protocolVersion"_s, u"2024-11-05"_s}})));
    EXPECT_EQ(body(older).value(u"result").toObject().value(u"protocolVersion").toString(), u"2025-11-25");

    // A notification is accepted with nothing to answer.
    const Editor::McpHttpReply notified =
        send(post({{u"jsonrpc"_s, u"2.0"_s}, {u"method"_s, u"notifications/initialized"_s}}));
    EXPECT_EQ(notified.status, 202);
    EXPECT_TRUE(notified.body.isEmpty());

    // tools/list: every command but the hidden ones, dots as underscores, queries read-only, sorted.
    const Editor::McpHttpReply listed =
        send(post(request(3, u"tools/list"_s), {{"mcp-protocol-version", "2025-06-18"}}));
    const QJsonArray tools = body(listed).value(u"result").toObject().value(u"tools").toArray();
    ASSERT_FALSE(tools.isEmpty());
    QStringList names;
    for (const QJsonValue& tool : tools)
    {
        names.append(tool.toObject().value(u"name").toString());
    }
    EXPECT_TRUE(names.contains(u"test_echo"_s));
    EXPECT_TRUE(names.contains(u"app_quit"_s));
    EXPECT_FALSE(names.contains(u"settings_set"_s)) << "hidden";
    QStringList sorted = names;
    sorted.sort();
    EXPECT_EQ(names, sorted);
    for (const QJsonValue& tool : tools)
    {
        const QJsonObject object = tool.toObject();
        EXPECT_FALSE(object.value(u"name").toString().contains(u'.'));
        EXPECT_EQ(object.value(u"inputSchema").toObject().value(u"type").toString(), u"object");
        if (object.value(u"name") == u"test_echo"_s)
        {
            EXPECT_TRUE(object.value(u"annotations").toObject().value(u"readOnlyHint").toBool());
            EXPECT_EQ(object.value(u"title").toString(), u"test.echo");
        }
    }

    // tools/call runs the command; the result is text JSON and structured content.
    const Editor::McpHttpReply called = send(
        post(request(4, u"tools/call"_s, {{u"name"_s, u"test_echo"_s}, {u"arguments"_s, QJsonObject{{u"a"_s, 5}}}})));
    const QJsonObject callResult = body(called).value(u"result").toObject();
    EXPECT_FALSE(callResult.value(u"isError").toBool());
    EXPECT_EQ(callResult.value(u"structuredContent").toObject().value(u"a").toInt(), 5);
    EXPECT_EQ(callResult.value(u"content").toArray().at(0).toObject().value(u"type").toString(), u"text");

    // A version that isn't served.
    const Editor::McpHttpReply wrong = send(post(request(5, u"ping"_s), {{"mcp-protocol-version", "2023-01-01"}}));
    EXPECT_EQ(wrong.status, 400);
}

// The per-request era: version and capabilities in _meta, headers that must mirror the body, server/discover.
TEST_F(McpProtocolTest, ModernRequests)
{
    const auto [discover, discoverHeaders] = modern(1, u"server/discover"_s);
    const Editor::McpHttpReply discovered = send(post(discover, discoverHeaders));
    ASSERT_EQ(discovered.status, 200);
    const QJsonObject result = body(discovered).value(u"result").toObject();
    EXPECT_TRUE(result.value(u"supportedVersions").toArray().contains(u"2026-07-28"_s));
    EXPECT_EQ(result.value(u"resultType").toString(), u"complete");

    const auto [call, callHeaders] = modern(2, u"tools/call"_s, {{u"name"_s, u"test_echo"_s}});
    EXPECT_EQ(send(post(call, callHeaders)).status, 200);

    // A header that disagrees with the body: HeaderMismatch.
    QHash<QByteArray, QByteArray> mismatched = callHeaders;
    mismatched.insert("mcp-name", "test_fail");
    const Editor::McpHttpReply refused = send(post(call, mismatched));
    EXPECT_EQ(refused.status, 400);
    EXPECT_EQ(body(refused).value(u"error").toObject().value(u"code").toInt(), -32020);

    // A base64-encoded header value means the same.
    QHash<QByteArray, QByteArray> encoded = callHeaders;
    encoded.insert("mcp-name", "=?base64?" + QByteArray("test_echo").toBase64() + "?=");
    EXPECT_EQ(send(post(call, encoded)).status, 200);

    // An unsupported version: its error lists the supported ones.
    QJsonObject future = call;
    QJsonObject params = future.value(u"params").toObject();
    QJsonObject meta = params.value(u"_meta").toObject();
    meta.insert(u"io.modelcontextprotocol/protocolVersion", u"2099-01-01"_s);
    params.insert(u"_meta", meta);
    future.insert(u"params", params);
    const Editor::McpHttpReply unsupported = send(post(future, callHeaders));
    EXPECT_EQ(unsupported.status, 400);
    const QJsonObject error = body(unsupported).value(u"error").toObject();
    EXPECT_EQ(error.value(u"code").toInt(), -32022);
    EXPECT_TRUE(error.value(u"data").toObject().value(u"supported").toArray().contains(u"2025-06-18"_s));

    // Without the client's capabilities.
    QJsonObject bare = call;
    meta = call.value(u"params").toObject().value(u"_meta").toObject();
    meta.remove(u"io.modelcontextprotocol/clientCapabilities");
    params = call.value(u"params").toObject();
    params.insert(u"_meta", meta);
    bare.insert(u"params", params);
    EXPECT_EQ(body(send(post(bare, callHeaders))).value(u"error").toObject().value(u"code").toInt(), -32602);

    // An unknown method is 404 in this era.
    const auto [unknown, unknownHeaders] = modern(3, u"resources/list"_s);
    EXPECT_EQ(send(post(unknown, unknownHeaders)).status, 404);
}

// The transport's checks: method, Origin (DNS rebinding), the token, the JSON.
TEST_F(McpProtocolTest, RefusesBadRequests)
{
    Editor::McpHttpRequest get = post(request(1, u"ping"_s));
    get.method = "GET";
    EXPECT_EQ(send(get).status, 405);

    EXPECT_EQ(send(post(request(1, u"ping"_s), {{"origin", "http://127.0.0.1:3000"}})).status, 200);
    EXPECT_EQ(send(post(request(1, u"ping"_s), {{"origin", "http://localhost"}})).status, 200);
    EXPECT_EQ(send(post(request(1, u"ping"_s), {{"origin", "https://evil.example"}})).status, 403);
    EXPECT_EQ(send(post(request(1, u"ping"_s), {{"origin", "http://127.0.0.1.evil.example"}})).status, 403);

    Editor::McpHttpRequest noToken = post(request(1, u"ping"_s));
    noToken.headers.remove("authorization");
    const Editor::McpHttpReply unauthorised = send(noToken);
    EXPECT_EQ(unauthorised.status, 401);
    EXPECT_FALSE(unauthorised.headers.empty()) << "WWW-Authenticate";
    Editor::McpHttpRequest wrongToken = post(request(1, u"ping"_s));
    wrongToken.headers.insert("authorization", "Bearer test-token-0123456789abcdeX");
    EXPECT_EQ(send(wrongToken).status, 401);

    Editor::McpHttpRequest broken = post({});
    broken.body = "{not json";
    EXPECT_EQ(body(send(broken)).value(u"error").toObject().value(u"code").toInt(), -32700);
    EXPECT_EQ(send(post({{u"jsonrpc"_s, u"1.0"_s}, {u"id"_s, 1}, {u"method"_s, u"ping"_s}})).status, 400);
}

// Failing commands, unknown tools, pictures and deferred results.
TEST_F(McpProtocolTest, ErrorsImagesAndDeferred)
{
    const QJsonObject failed =
        body(send(post(request(1, u"tools/call"_s, {{u"name"_s, u"test_fail"_s}})))).value(u"result").toObject();
    EXPECT_TRUE(failed.value(u"isError").toBool()) << "a tool error the model can read, not a protocol error";
    EXPECT_TRUE(failed.value(u"content").toArray().at(0).toObject().value(u"text").toString().contains(u"always"));

    const QJsonObject unknown = body(send(post(request(2, u"tools/call"_s, {{u"name"_s, u"no_such_tool"_s}}))));
    EXPECT_EQ(unknown.value(u"error").toObject().value(u"code").toInt(), -32602);
    const QJsonObject hidden = body(send(post(request(3, u"tools/call"_s, {{u"name"_s, u"settings_set"_s}}))));
    EXPECT_EQ(hidden.value(u"error").toObject().value(u"code").toInt(), -32602) << "hidden tools can't be called";

    // A png field becomes image content, and leaves the text.
    const QJsonObject picture =
        body(send(post(request(4, u"tools/call"_s, {{u"name"_s, u"test_picture"_s}})))).value(u"result").toObject();
    const QJsonArray content = picture.value(u"content").toArray();
    ASSERT_EQ(content.size(), 2);
    EXPECT_FALSE(content.at(0).toObject().value(u"text").toString().contains(u"iVBOR"));
    EXPECT_EQ(content.at(1).toObject().value(u"type").toString(), u"image");
    EXPECT_EQ(content.at(1).toObject().value(u"mimeType").toString(), u"image/png");
    EXPECT_EQ(content.at(1).toObject().value(u"data").toString(), u"iVBORw0K");

    // Deferred: handled later, by polling its query until it isn't waiting.
    Editor::McpStep step = m_protocol.handle(post(request(5, u"tools/call"_s, {{u"name"_s, u"test_later"_s}})));
    ASSERT_TRUE(step.pending);
    EXPECT_EQ(step.pending->query, u"test.ready");
    int polls = 0;
    while (step.pending && polls < 10)
    {
        step = m_protocol.resume(*step.pending);
        ++polls;
    }
    ASSERT_TRUE(step.reply);
    EXPECT_EQ(polls, 3);
    const QJsonObject later = body(*step.reply);
    EXPECT_EQ(later.value(u"id").toInt(), 5);
    EXPECT_EQ(later.value(u"result").toObject().value(u"structuredContent").toObject().value(u"n").toInt(), 7);

    const Editor::McpHttpReply late =
        Editor::McpProtocol::timedOut({.id = QJsonValue(6), .query = u"test.ready"_s, .parameters = {}});
    EXPECT_TRUE(body(late).value(u"result").toObject().value(u"isError").toBool());
}

// Over real HTTP on 127.0.0.1: the client on a thread of its own, the commands on this (GUI) thread.
TEST_F(McpHttpTest, ServesOverHttp)
{
    Editor::McpHttpServer server{m_protocol, m_log, std::chrono::seconds(5)};
    const Meow2::Result<std::uint16_t> port = server.start(0);
    ASSERT_TRUE(port) << port.error().message;
    EXPECT_TRUE(server.running());

    struct Answer
    {
        int status = 0;
        std::string body;
    };
    const auto call = [listening = *port](std::string json, bool token) -> Answer
    {
        httplib::Client client("127.0.0.1", listening);
        client.set_read_timeout(std::chrono::seconds(10));
        httplib::Headers headers{{"MCP-Protocol-Version", "2025-06-18"}};
        if (token)
        {
            headers.emplace("Authorization", std::string("Bearer ") + c_token);
        }
        const httplib::Result result = client.Post("/mcp", headers, json, "application/json");
        return result ? Answer{result->status, result->body} : Answer{};
    };
    // Waits for the client while the event loop runs the queued requests.
    const auto wait = [](std::future<Answer>& future)
    {
        while (future.wait_for(std::chrono::milliseconds(5)) != std::future_status::ready)
        {
            QCoreApplication::processEvents();
        }
        return future.get();
    };

    std::future<Answer> echoed = std::async(std::launch::async, call,
                                            R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":)"
                                            R"({"name":"test_echo","arguments":{"x":"y"}}})",
                                            true);
    const Answer echo = wait(echoed);
    EXPECT_EQ(echo.status, 200);
    EXPECT_NE(echo.body.find(R"("x":"y")"), std::string::npos) << echo.body;

    std::future<Answer> refused =
        std::async(std::launch::async, call, R"({"jsonrpc":"2.0","id":2,"method":"ping"})", false);
    EXPECT_EQ(wait(refused).status, 401);

    // A deferred call: the transport polls between event-loop turns until the answer is there.
    std::future<Answer> deferred =
        std::async(std::launch::async, call,
                   R"({"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"test_later"}})", true);
    const Answer later = wait(deferred);
    EXPECT_EQ(later.status, 200);
    EXPECT_NE(later.body.find(R"("n":7)"), std::string::npos) << later.body;

    server.stop();
    EXPECT_FALSE(server.running());
    EXPECT_EQ(server.port(), 0);
}

// The integration test of techstack §20: a headless editor (no GPU needed: editor.render off) started with --mcp,
// driven as an agent would: list the tools, build a scene, save it, quit.
TEST_F(McpHeadlessTest, AgentBuildsAScene)
{
    const std::filesystem::path folder =
        std::filesystem::temp_directory_path() / std::format("meow2-mcp-headless-{}", Meow2::c_configName);
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
    std::filesystem::create_directories(folder);

    QProcess editor;
    editor.setProcessChannelMode(QProcess::MergedChannels);
    editor.start(QString::fromUtf8(MEOW2_EDITOR_EXE),
                 {u"--headless"_s, u"--mcp"_s, u"--mcp-token"_s, QString::fromLatin1(c_token), u"--config"_s,
                  QString::fromStdWString((folder / "Meow2Editor.toml").wstring()), u"--log-dir"_s,
                  QString::fromStdWString((folder / "logs").wstring()), u"--set"_s, u"editor.render=false"_s,
                  u"--set"_s, u"editor.mcp.port=0"_s, u"--set"_s,
                  u"editor.project=\""_s +
                      QDir::fromNativeSeparators(QString::fromStdWString((folder / "project").wstring())) + u"\""_s});
    ASSERT_TRUE(editor.waitForStarted(10000));

    // It prints the address once it listens (port 0: a free one).
    QByteArray output;
    std::optional<std::uint16_t> port;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    const QRegularExpression address(u"MCP server on http://127\\.0\\.0\\.1:(\\d+)/mcp"_s);
    while (!port && std::chrono::steady_clock::now() < deadline && editor.state() == QProcess::Running)
    {
        editor.waitForReadyRead(200);
        output += editor.readAll();
        if (const QRegularExpressionMatch match = address.match(QString::fromUtf8(output)); match.hasMatch())
        {
            port = static_cast<std::uint16_t>(match.captured(1).toUInt());
        }
    }
    ASSERT_TRUE(port) << output.toStdString();

    httplib::Client client("127.0.0.1", *port);
    client.set_read_timeout(std::chrono::seconds(20));
    int nextId = 1;
    const auto rpc = [&](const QString& method, const QJsonObject& params) -> QJsonObject
    {
        const QJsonObject message{
            {u"jsonrpc"_s, u"2.0"_s}, {u"id"_s, nextId++}, {u"method"_s, method}, {u"params"_s, params}};
        const httplib::Result result =
            client.Post("/mcp",
                        httplib::Headers{{"Authorization", std::string("Bearer ") + c_token},
                                         {"MCP-Protocol-Version", "2025-11-25"}},
                        QJsonDocument(message).toJson(QJsonDocument::Compact).toStdString(), "application/json");
        EXPECT_TRUE(result) << "the editor answers";
        return result ? QJsonDocument::fromJson(QByteArray::fromStdString(result->body)).object() : QJsonObject{};
    };
    const auto tool = [&](const QString& name, const QJsonObject& arguments) -> QJsonObject
    {
        const QJsonObject result =
            rpc(u"tools/call"_s, {{u"name"_s, name}, {u"arguments"_s, arguments}}).value(u"result").toObject();
        EXPECT_FALSE(result.value(u"isError").toBool())
            << name.toStdString() << ": " << QJsonDocument(result).toJson().toStdString();
        return result.value(u"structuredContent").toObject();
    };

    const QJsonObject init = rpc(u"initialize"_s, {{u"protocolVersion"_s, u"2025-11-25"_s},
                                                   {u"capabilities"_s, QJsonObject{}},
                                                   {u"clientInfo"_s, QJsonObject{{u"name"_s, u"test"_s}}}});
    EXPECT_EQ(init.value(u"result").toObject().value(u"protocolVersion").toString(), u"2025-11-25");
    const QJsonArray tools = rpc(u"tools/list"_s, {}).value(u"result").toObject().value(u"tools").toArray();
    EXPECT_GT(tools.size(), 30) << "every editor command";

    const QJsonObject created = tool(u"entity_create"_s, {{u"name"_s, u"Built by an agent"_s}});
    EXPECT_FALSE(created.value(u"id").toString().isEmpty());
    tool(u"scene_saveAs"_s, {{u"path"_s, u"Scenes/Agent.m2scene"_s}});
    // Without a GPU there's no picture: an error the agent can read, not a hang.
    const QJsonObject shot = rpc(u"tools/call"_s, {{u"name"_s, u"viewport_screenshot"_s}}).value(u"result").toObject();
    EXPECT_TRUE(shot.value(u"isError").toBool());
    tool(u"app_quit"_s, {});

    ASSERT_TRUE(editor.waitForFinished(30000)) << "app.quit ends it";
    EXPECT_EQ(editor.exitCode(), 0) << (output + editor.readAll()).toStdString();
    std::ifstream file(folder / "project" / "Scenes" / "Agent.m2scene");
    const std::string scene{std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
    EXPECT_NE(scene.find("Built by an agent"), std::string::npos) << scene;
}
