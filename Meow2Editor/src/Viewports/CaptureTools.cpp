/// @file
/// @brief CaptureTools (CaptureTools.h).

#include "Viewports/CaptureTools.h"

#include "Commands/CommandSchema.h"
#include "Viewports/EditorRenderer.h"

#include <Meow2/Assets/Texture/Image.h>

#include <algorithm>
#include <QByteArray>
#include <QJsonArray>

namespace Meow2::Editor
{
using namespace Qt::StringLiterals;

namespace
{
QJsonValue number(double value)
{
    return QJsonValue{value};
}

/// @brief A whole number from JSON, clamped to 0 and up.
std::uint32_t index(const QJsonValue& value)
{
    return static_cast<std::uint32_t>(std::max(0.0, value.toDouble()));
}
} // namespace

QJsonObject flowToJson(const Renderer::FrameFlow& flow)
{
    QJsonArray resources;
    for (std::uint32_t i = 0; i < flow.resources.size(); ++i)
    {
        const Renderer::FlowResource& resource = flow.resources[i];
        QJsonObject json{{u"name"_s, QString::fromStdString(resource.name)},
                         {u"texture"_s, resource.texture},
                         {u"debug"_s, resource.debug},
                         {u"kind"_s, QString::fromUtf8(Renderer::resourceKindName(Renderer::resourceKind(flow, i)))}};
        if (resource.texture)
        {
            json.insert(u"width", number(resource.info.width));
            json.insert(u"height", number(resource.info.height));
            json.insert(u"format", number(static_cast<double>(resource.info.format)));
            json.insert(u"formatName", QString::fromUtf8(Renderer::formatName(resource.info.format)));
        }
        resources.append(json);
    }
    QJsonArray passes;
    for (const Renderer::FlowPass& pass : flow.passes)
    {
        QJsonArray accesses;
        for (const Renderer::FlowAccess& access : pass.accesses)
        {
            accesses.append(QJsonObject{{u"resource"_s, number(access.resource)},
                                        {u"state"_s, number(static_cast<double>(access.state))},
                                        {u"stateName"_s, QString::fromUtf8(Renderer::resourceStateName(access.state))},
                                        {u"writes"_s, access.writes}});
        }
        passes.append(QJsonObject{{u"name"_s, QString::fromStdString(pass.name)},
                                  {u"debug"_s, pass.debug},
                                  {u"skipped"_s, pass.skipped},
                                  {u"accesses"_s, accesses}});
    }
    return QJsonObject{{u"passes"_s, passes}, {u"resources"_s, resources}};
}

Renderer::FrameFlow flowFromJson(const QJsonObject& json)
{
    Renderer::FrameFlow flow;
    for (const QJsonValue& value : json.value(u"resources").toArray())
    {
        const QJsonObject resource = value.toObject();
        Renderer::FlowResource out;
        out.name = resource.value(u"name").toString().toStdString();
        out.texture = resource.value(u"texture").toBool();
        out.debug = resource.value(u"debug").toBool();
        if (out.texture)
        {
            out.info = {.width = index(resource.value(u"width")),
                        .height = index(resource.value(u"height")),
                        .format = static_cast<Rhi::RhiFormat>(index(resource.value(u"format")))};
        }
        flow.resources.push_back(std::move(out));
    }
    for (const QJsonValue& value : json.value(u"passes").toArray())
    {
        const QJsonObject pass = value.toObject();
        Renderer::FlowPass out{.name = pass.value(u"name").toString().toStdString(),
                               .accesses = {},
                               .debug = pass.value(u"debug").toBool(),
                               .skipped = pass.value(u"skipped").toBool()};
        for (const QJsonValue& accessValue : pass.value(u"accesses").toArray())
        {
            const QJsonObject access = accessValue.toObject();
            const std::uint32_t resource = index(access.value(u"resource"));
            if (resource >= flow.resources.size())
            {
                continue; // names a resource that isn't there: dropped, so nothing indexes past the end
            }
            out.accesses.push_back({.resource = resource,
                                    .state = static_cast<Rhi::ResourceState>(index(access.value(u"state"))),
                                    .writes = access.value(u"writes").toBool()});
        }
        flow.passes.push_back(std::move(out));
    }
    return flow;
}

CaptureTools::CaptureTools(EditorCommandRegistry& commands, EditorRenderer* renderer, Log::Logger& log)
    : m_commands(commands)
    , m_renderer(renderer)
    , m_log(log)
{
    registerCommands();
}

CaptureTools::~CaptureTools()
{
    for (const auto* name : {u"capture.frame", u"capture.clear", u"capture.info", u"capture.show", u"capture.image"})
    {
        m_commands.remove(QStringView{name});
    }
}

void CaptureTools::tick()
{
    if (m_renderer == nullptr)
    {
        return;
    }
    if (std::optional<Renderer::CapturedImage> image = m_renderer->frameCapture().takeImage())
    {
        m_image = std::move(*image);
        ++m_imageSerial;
    }
}

void CaptureTools::registerCommands()
{
    EditorCommandRegistry& commands = m_commands;
    const auto noGpu = []
    {
        return fail(ErrorCode::Unsupported, "the editor draws nothing without a GPU, so there's no frame to capture");
    };

    [[maybe_unused]] const Result<void> frame = commands.add(
        {.name = u"capture.frame"_s,
         .description = u"Captures the next frame drawn (F9): its render graph and every texture as each pass left it. "
                        u"Replaces the last capture."_s},
        [this, noGpu](const QJsonObject&) -> Result<CommandOutcome>
        {
            if (m_renderer == nullptr)
            {
                return noGpu();
            }
            m_renderer->frameCapture().request();
            return CommandOutcome{.result = QJsonObject{{u"requested"_s, true}}};
        });

    [[maybe_unused]] const Result<void> clear =
        commands.add({.name = u"capture.clear"_s, .description = u"Drops the capture and frees its textures."_s},
                     [this](const QJsonObject&) -> Result<CommandOutcome>
                     {
                         if (m_renderer != nullptr)
                         {
                             m_renderer->frameCapture().clear();
                         }
                         m_image.reset();
                         ++m_imageSerial;
                         return CommandOutcome{};
                     });

    [[maybe_unused]] const Result<void> info = commands.add(
        {.name = u"capture.info"_s,
         .description = u"The capture: its frame number, its render graph (passes, what each reads and writes, the "
                        u"resources) and which textures were kept. With brief, only whether there is one."_s,
         .kind = CommandKind::Query,
         .parameters =
             objectSchema({{.name = u"brief"_s, .schema = propertySchema(u"boolean"_s, u"Leave the graph out."_s)}})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            if (m_renderer == nullptr || !m_renderer->frameCapture().flow())
            {
                return CommandOutcome{
                    .result =
                        QJsonObject{{u"captured"_s, false},
                                    {u"pending"_s, m_renderer != nullptr && m_renderer->frameCapture().requested()}}};
            }
            const Renderer::FrameCapture& capture = m_renderer->frameCapture();
            QJsonObject result{{u"captured"_s, true},
                               {u"pending"_s, capture.requested()},
                               {u"frame"_s, number(static_cast<double>(capture.frame()))}};
            if (!parameters.value(u"brief").toBool())
            {
                result.insert(u"flow", flowToJson(*capture.flow()));
                QJsonArray snapshots;
                for (const Renderer::CaptureSnapshot& snapshot : capture.snapshots())
                {
                    snapshots.append(QJsonObject{{u"pass"_s, number(snapshot.pass)},
                                                 {u"resource"_s, number(snapshot.resource)},
                                                 {u"depth"_s, snapshot.depth}});
                }
                result.insert(u"snapshots", snapshots);
            }
            return CommandOutcome{.result = result};
        });

    QJsonObject channels = propertySchema(u"integer"_s, u"Bits 0..3: R, G, B, A shown; one bit alone as grey."_s);
    channels.insert(u"minimum", 1);
    channels.insert(u"maximum", 15);
    QJsonObject maxSize = propertySchema(u"integer"_s, u"The picture's longest side at most, in pixels."_s);
    maxSize.insert(u"minimum", 16);
    maxSize.insert(u"maximum", 4096);
    [[maybe_unused]] const Result<void> show = commands.add(
        {.name = u"capture.show"_s,
         .description = u"Asks for a captured texture as a picture: the one `pass` left of `resource` (indices from "
                        u"capture.info). It comes back a frame or two later (capture.image)."_s,
         .parameters = objectSchema(
             {{.name = u"pass"_s,
               .schema = propertySchema(u"integer"_s, u"The pass that wrote it."_s),
               .required = true},
              {.name = u"resource"_s, .schema = propertySchema(u"integer"_s, u"The resource."_s), .required = true},
              {.name = u"channels"_s, .schema = channels},
              {.name = u"rangeMin"_s, .schema = propertySchema(u"number"_s, u"The value shown black."_s)},
              {.name = u"rangeMax"_s, .schema = propertySchema(u"number"_s, u"The value shown white."_s)},
              {.name = u"maxSize"_s, .schema = maxSize}})},
        [this, noGpu](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            if (m_renderer == nullptr)
            {
                return noGpu();
            }
            Renderer::FrameCapture& capture = m_renderer->frameCapture();
            const std::optional<std::uint32_t> snapshot =
                capture.snapshotFor(index(parameters.value(u"pass")), index(parameters.value(u"resource")));
            if (!snapshot)
            {
                return fail(ErrorCode::NotFound,
                            "that pass didn't write that texture in the capture (or there's none)");
            }
            Renderer::TextureDisplaySettings settings =
                Renderer::defaultDisplaySettings(capture.snapshots()[*snapshot].depth);
            if (parameters.contains(u"channels"))
            {
                settings.channels = index(parameters.value(u"channels"));
            }
            if (parameters.contains(u"rangeMin"))
            {
                settings.rangeMin = static_cast<float>(parameters.value(u"rangeMin").toDouble());
            }
            if (parameters.contains(u"rangeMax"))
            {
                settings.rangeMax = static_cast<float>(parameters.value(u"rangeMax").toDouble());
            }
            if (settings.rangeMax <= settings.rangeMin)
            {
                return fail(ErrorCode::InvalidArgument, "rangeMax must be above rangeMin");
            }
            capture.requestImage(*snapshot, settings,
                                 parameters.contains(u"maxSize") ? index(parameters.value(u"maxSize")) : 512U);
            // Deferred for MCP: the call waits until this picture is back (capture.image with `after`), then answers
            // with it. The dock ignores that part and watches capture.image's serial itself.
            return CommandOutcome{
                .result = QJsonObject{
                    {u"snapshot"_s, number(*snapshot)},
                    {u"deferred"_s,
                     QJsonObject{
                         {u"query"_s, u"capture.image"_s},
                         {u"parameters"_s,
                          QJsonObject{{u"data"_s, true}, {u"after"_s, number(static_cast<double>(m_imageSerial))}}}}}}};
        });

    [[maybe_unused]] const Result<void> image = commands.add(
        {.name = u"capture.image"_s,
         .description =
             u"The last captured texture shown as a picture: serial (rises with each new one), snapshot, "
             u"size, settings, pending; with data, the picture as a base64 PNG; with after (a serial), {waiting: true} "
             u"until a newer picture is back."_s,
         .kind = CommandKind::Query,
         .parameters = objectSchema(
             {{.name = u"data"_s, .schema = propertySchema(u"boolean"_s, u"Include the PNG (base64)."_s)},
              {.name = u"after"_s,
               .schema = propertySchema(u"integer"_s, u"Wait for a picture with a serial above this one."_s)}})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            const bool pending = m_renderer != nullptr && m_renderer->frameCapture().imagePending();
            if (parameters.contains(u"after") &&
                (pending || static_cast<double>(m_imageSerial) <= parameters.value(u"after").toDouble()))
            {
                return CommandOutcome{.result = QJsonObject{{u"waiting"_s, true}}}; // not back yet (MCP polls)
            }
            QJsonObject result{{u"serial"_s, number(static_cast<double>(m_imageSerial))},
                               {u"pending"_s, pending},
                               {u"ready"_s, m_image.has_value()}};
            if (m_image)
            {
                result.insert(u"snapshot", number(m_image->snapshot));
                result.insert(u"width", number(m_image->width));
                result.insert(u"height", number(m_image->height));
                result.insert(u"channels", number(m_image->settings.channels));
                result.insert(u"rangeMin", number(static_cast<double>(m_image->settings.rangeMin)));
                result.insert(u"rangeMax", number(static_cast<double>(m_image->settings.rangeMax)));
                if (parameters.value(u"data").toBool())
                {
                    // Opaque: the display pass writes alpha 1, and a PNG viewer shouldn't show a checkerboard.
                    const Result<std::vector<std::byte>> png = Assets::encodePng(
                        {.width = m_image->width, .height = m_image->height, .pixels = m_image->pixels});
                    if (!png)
                    {
                        return Unexpected<Error>{png.error()};
                    }
                    const QByteArray bytes(reinterpret_cast<const char*>(png->data()),
                                           static_cast<qsizetype>(png->size()));
                    result.insert(u"png", QString::fromLatin1(bytes.toBase64()));
                }
            }
            return CommandOutcome{.result = result};
        });
}
} // namespace Meow2::Editor
