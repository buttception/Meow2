# MCP server (Meow2Editor)

Meow2Editor has a built-in **MCP server** (Model Context Protocol): an AI agent such as Claude Code can drive the editor with the same commands its menus use. It builds scenes, edits components, saves files and looks at the result as screenshots. Every editor command is an MCP tool (techstack.md §20), so a feature that has a button can also be used by an agent.

## Using it

Start the editor with the server on:

```
Meow2Editor.exe --mcp
```

Then **Help → Copy MCP setup** copies the line that connects Claude Code to it, with this run's address and token:

```
claude mcp add --transport http meow2 http://127.0.0.1:47717/mcp --header "Authorization: Bearer 3f9c…"
```

Run it once in a terminal, start Claude Code, and the editor's tools are there (`entity_create`, `component_setField`, `scene_save`, `viewport_screenshot`, …). Changes an agent makes appear in the editor at once, and Ctrl+Z undoes them like your own.

### Options

| Option | Setting (`Meow2Editor.toml`) | What it does |
|---|---|---|
| `--mcp` | `editor.mcp.enabled` (false) | Starts the server. |
| `--mcp-port <n>` | `editor.mcp.port` (47717) | Its port, on 127.0.0.1. With the setting at 0, a free port is used (the editor prints the address). |
| `--mcp-token <text>` | — | The bearer token, at least 16 characters. Without it, each run makes a random 64-character one. |
| `--headless` | `editor.headless.width`, `editor.headless.height` (1280 × 720) | No window: the viewports draw into offscreen textures of this size. Combined with `--mcp`, an agent drives an editor nobody sees. |

A random token is never written to the log, because logs get shared. Help → Copy MCP setup has it, and an agent that starts the editor itself passes its own with `--mcp-token`.

### Headless

```
Meow2Editor.exe --headless --mcp --mcp-token <token> --set editor.project=D:/Games/MyGame
```

The editor runs on Qt's offscreen platform, so no window opens and no desktop is needed. With a GPU, both viewports are drawn into offscreen textures, and `viewport_screenshot` shows them. Without one (`--set editor.render=false`, or no GPU), everything except drawing still works. Headless frames are paced at 16 ms: no display waits for them. `app_quit` ends the run.

## Tools

The tool list is generated from the command registry, so it always matches the editor:

- **Name:** the command's name with dots as underscores: `entity.create` → `entity_create`. Claude's tool names allow only letters, digits, `_` and `-`. The original name is the tool's `title`.
- **Description and input schema:** the command's own. Its JSON Schema goes to the client unchanged.
- **Annotations:** queries are `readOnlyHint`; nothing is destructive in a way undo can't fix, and nothing reaches outside the editor (`destructiveHint` and `openWorldHint` false).
- **Results:** a command's JSON result is returned as text and as `structuredContent`. A result with a `png` field (base64) also returns the picture as **image content**, so the model sees it.
- **Errors:** a command that fails returns a result with `isError` and the error message, so the model can correct its call. A tool that doesn't exist is a JSON-RPC error.
- **Not offered:** `project.open` and `project.new` (they could point the editor at any folder; agents work in the project the editor was started with) and `mcp.copySetup`.

Tools made for agents:

| Tool | What it does |
|---|---|
| `viewport_screenshot {viewport, maxSize}` | A PNG of the editor (default) or game view, scaled to at most `maxSize` px (1024). Fails without a GPU, for a hidden view, or for the game view without a main camera. |
| `capture_frame`, then `capture_info`, `capture_show` | A frame capture: the render graph as JSON, and any texture as a pass left it, as a picture ([Editor → Frame capture](Editor.md#frame-capture-and-the-render-graph)). |
| `log_lines {since, levels, text}` | The editor's log, to read what a failed step said. |
| `app_quit` | Closes the editor (unsaved changes are lost). |

## How it works

```
Claude Code ──HTTP POST /mcp──► cpp-httplib (its threads) ──queued call──► GUI thread: McpProtocol ──► EditorCommandRegistry
            ◄──── JSON reply ────────────── waits on a future ◄──────────────── reply, or "pending": poll a query each frame
```

- **`McpHttpServer`** (`src/Mcp/`) runs cpp-httplib on its own thread, bound to **127.0.0.1 only**. A request is never handled on the network thread. It's handed to the GUI thread with a queued `QMetaObject::invokeMethod`, where commands may run, and the network thread waits for the answer. So the GUI thread never waits for the network, and the network threads never touch editor state.
- **`McpProtocol`** does everything but the networking, which is why it can be tested without sockets:
  1. **Transport checks:** POST only (405); an `Origin` header that isn't 127.0.0.1, localhost or ::1 is refused (403, see [DNS rebinding](#why-its-done-this-way)); the bearer token, compared in constant time (401).
  2. **JSON-RPC:** parse errors are -32700, malformed requests -32600, unknown methods -32601. Notifications (no `id`) get 202 and no body.
  3. **The protocol version,** in two eras (below).
  4. **Methods:** `initialize`, `server/discover`, `ping`, `tools/list`, `tools/call`.
- **Deferred results:** a screenshot needs the next frame to be drawn and copied back from the GPU, a frame or two later. Its command doesn't wait (that would freeze the editor). It returns `{"deferred": {"query": "viewport.screenshotResult", "parameters": {…}}}`. The protocol turns that into a *pending* call, and the transport runs the query again every 15 ms, between frames, until it no longer answers `{"waiting": true}`, then replies. After 10 s it gives up with a tool error. `capture.show` works the same way, with `capture.image {after}` as its query.
- **Screenshots** (`EditorRenderer::requestScreenshot()`): at the end of the next frame that draws the viewport, a command list copies its final image (the swapchain's, or the offscreen texture's when headless) into a readback buffer of that frame slot. The copy is read when the slot comes round again, after its fence. `ViewportController` scales the picture with `QImage` and encodes it as a PNG. Nothing waits on the GPU.
- **Shutting down:** `stop()` first marks the server as stopping, so a request waiting for the GUI thread gives up with 503, then stops httplib and joins its thread. A request can't keep the editor from closing.

### Protocol versions

MCP changed shape in its **2026-07-28** revision. Both shapes are served, because today's clients still use the older one:

- **Legacy (2025-03-26, 2025-06-18, 2025-11-25):** the client sends `initialize` and gets a version and capabilities. Later requests carry the version in the `MCP-Protocol-Version` header; without it, 2025-03-26 is assumed.
- **Modern (2026-07-28):** no handshake. Every request carries `_meta` with `io.modelcontextprotocol/protocolVersion` and `…/clientCapabilities`. `server/discover` describes the server.
  - The headers `MCP-Protocol-Version`, `Mcp-Method` and, for `tools/call`, `Mcp-Name` must match the body. A mismatch is 400, HeaderMismatch (-32020). Values may be base64-encoded as `=?base64?…?=`.
  - An unsupported version is 400 with UnsupportedProtocolVersion (-32022) and the supported list.
  - Every result has `resultType: "complete"`.

Neither era needs sessions: the editor keeps nothing per client.

## Why it's done this way

- **The command registry is the only door.** The MCP server adds no editor features of its own; it lists and runs commands. So what an agent can do is exactly what the UI can do, it's undoable the same way, and a new command is a new tool without any MCP code.
- **localhost, a token and an Origin check.** A server on 127.0.0.1 can't be reached from other machines, but a web page in your browser can still send requests to 127.0.0.1. **DNS rebinding** makes that worse: a page on `evil.example` changes its DNS record to point at 127.0.0.1, and the browser then treats your editor as the same site. The browser still sends the page's own `Origin`, so refusing foreign origins stops it (the MCP specification requires this check). The random token is a second lock: a page doesn't know it. It's off unless asked for, because an editor that doesn't serve agents shouldn't listen at all.
- **cpp-httplib** (MIT, one header) rather than Qt's `QHttpServer`. `QHttpServer` is GPL or commercial, not LGPL like the rest of the Qt modules we use (owner decision 2026-11-14). It runs on its own threads, which suits the design above.
- **Deferred results instead of waiting.** Waiting on the GUI thread for the GPU would stall drawing, and drawing is what produces the picture. Polling a query between frames lets the frame happen.
- **Tool names with underscores.** MCP allows dots, but Claude's tool names don't, and the commands' dotted names are kept as titles.

**References:** the [MCP specification](https://modelcontextprotocol.io/specification) (transports, tools, security best practices), [cpp-httplib](https://github.com/yhirose/cpp-httplib), and the OWASP notes on [DNS rebinding](https://owasp.org/www-community/attacks/DNS_Rebinding).

## Tests

- `Editor.EditorArgumentsTest.ReadsOptions`: the options, and bad ports and short tokens refused.
- `Editor.McpProtocolTest.LegacyHandshakeAndTools`: `initialize` (a version the editor doesn't know gets its newest), notifications, `tools/list` (hidden commands left out, underscores, sorted, read-only queries), `tools/call`, an unsupported header version.
- `Editor.McpProtocolTest.ModernRequests`: `server/discover`, header mismatches (and base64 headers), an unsupported version with the supported list, missing capabilities, 404 for unknown methods.
- `Editor.McpProtocolTest.RefusesBadRequests`: GET, foreign origins (including `127.0.0.1.evil.example`), missing and wrong tokens, bad JSON.
- `Editor.McpProtocolTest.ErrorsImagesAndDeferred`: failing commands as tool errors, unknown and hidden tools, PNGs as image content, a deferred call answered on the third poll, the timeout answer.
- `Editor.McpHttpTest.ServesOverHttp`: over real HTTP, with the client on another thread: a call, a refused token, a deferred call; then stop.
- `Editor.McpHeadlessTest.AgentBuildsAScene`: starts `Meow2Editor --headless --mcp`, reads the address it prints, and works as an agent would: initialize, list the tools, create an entity, save the scene, see `viewport_screenshot` fail cleanly without a GPU, `app_quit`. Then it checks the saved file.
- `EditorGpu.EditorRendererTest.TakesAScreenshot`: the screenshot comes back a frame or two later at the viewport's size, opaque, the same pixels as the frame.
