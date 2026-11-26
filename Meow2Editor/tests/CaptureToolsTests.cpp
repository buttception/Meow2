/// @file
/// @brief Tests for the frame capture in the editor without a GPU: the `capture.*` commands, the graph as JSON both
///        ways, and the Render graph dock against stand-in commands (offscreen).

#include "Commands/CommandSchema.h"
#include "EditorTestRig.h"
#include "Panels/FlowView.h"
#include "Panels/RenderGraphDock.h"
#include "Viewports/CaptureTools.h"

#include <Meow2/Assets/Texture/Image.h>

#include <optional>
#include <QApplication>
#include <QByteArray>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPixmap>
#include <vector>

namespace
{
using namespace Qt::StringLiterals;
using CaptureCommandsTest = Meow2::Tests::EditorTest;
using RenderGraphDockTest = Meow2::Tests::EditorTest;
namespace Renderer = Meow2::Renderer;
namespace Rhi = Meow2::Rhi;

/// @brief A small frame: Cull writes the draw list; Shade reads it as indirect arguments and draws the colour;
///        Tone map reads the colour.
Renderer::FrameFlow smallFlow()
{
    Renderer::FrameFlow flow;
    flow.resources = {
        {.name = "Colour",
         .texture = true,
         .info = {.width = 64, .height = 32, .format = Rhi::RhiFormat::Rgba16Float},
         .debug = false},
        {.name = "Draws", .texture = false, .info = {}, .debug = false},
    };
    flow.passes = {
        {.name = "Cull", .accesses = {{.resource = 1, .state = Rhi::ResourceState::ShaderWrite, .writes = true}}},
        {.name = "Shade",
         .accesses = {{.resource = 1, .state = Rhi::ResourceState::IndirectArgs, .writes = false},
                      {.resource = 0, .state = Rhi::ResourceState::ColorTarget, .writes = true}}},
        {.name = "Tone map",
         .accesses = {{.resource = 0, .state = Rhi::ResourceState::ShaderRead, .writes = false}},
         .debug = false,
         .skipped = true},
    };
    return flow;
}
} // namespace

// Without a GPU there's nothing to capture: the commands say so instead of pretending.
TEST_F(CaptureCommandsTest, WithoutGpu)
{
    Meow2::Editor::EditorMainWindow window{windowDeps()};
    EXPECT_FALSE(m_commands.execute(u"capture.frame", {}));
    EXPECT_FALSE(m_commands.execute(u"capture.show", {{u"pass"_s, 0}, {u"resource"_s, 0}}));
    const QJsonObject info = m_commands.execute(u"capture.info", {}).value().toObject();
    EXPECT_FALSE(info.value(u"captured").toBool());
    EXPECT_FALSE(info.value(u"pending").toBool());
    const QJsonObject image = m_commands.execute(u"capture.image", {}).value().toObject();
    EXPECT_FALSE(image.value(u"ready").toBool());
    EXPECT_TRUE(m_commands.execute(u"capture.clear", {})) << "nothing to clear is fine";
    EXPECT_FALSE(m_history.canUndo());
}

TEST(CaptureFlowJson, RoundTrips)
{
    const Renderer::FrameFlow flow = smallFlow();
    const QJsonObject json = Meow2::Editor::flowToJson(flow);
    EXPECT_EQ(json.value(u"resources").toArray().at(1).toObject().value(u"kind").toString(), u"Indirect arguments"_s)
        << "a buffer read as indirect arguments";
    const Renderer::FrameFlow back = Meow2::Editor::flowFromJson(json);
    ASSERT_EQ(back.passes.size(), flow.passes.size());
    ASSERT_EQ(back.resources.size(), flow.resources.size());
    EXPECT_EQ(back.passes[1].name, "Shade");
    ASSERT_EQ(back.passes[1].accesses.size(), 2U);
    EXPECT_EQ(back.passes[1].accesses[0].state, Rhi::ResourceState::IndirectArgs);
    EXPECT_TRUE(back.passes[1].accesses[1].writes);
    EXPECT_TRUE(back.passes[2].skipped);
    EXPECT_EQ(back.resources[0].info.format, Rhi::RhiFormat::Rgba16Float);
    EXPECT_EQ(back.resources[0].info.width, 64U);

    // A broken one: an access to a resource that isn't there is dropped, not followed.
    QJsonObject broken = json;
    QJsonArray passes = broken.value(u"passes").toArray();
    QJsonObject pass = passes.at(0).toObject();
    pass.insert(u"accesses", QJsonArray{QJsonObject{{u"resource"_s, 99}, {u"state"_s, 0}, {u"writes"_s, true}}});
    passes.replace(0, pass);
    broken.insert(u"passes", passes);
    EXPECT_TRUE(Meow2::Editor::flowFromJson(broken).passes[0].accesses.empty());
}

// The dock draws the captured graph and asks for textures through the commands; here they're stand-ins that serve
// a small flow and a 2 × 2 picture, so it's tested without a GPU.
TEST_F(RenderGraphDockTest, ShowsAFlowAndPicks)
{
    std::optional<QJsonObject> shown;
    double serial = 0.0;
    ASSERT_TRUE(m_commands.add(
        {.name = u"capture.info"_s,
         .description = u"Stand-in."_s,
         .kind = Meow2::Editor::CommandKind::Query,
         .parameters = Meow2::Editor::objectSchema(
             {{.name = u"brief"_s, .schema = Meow2::Editor::propertySchema(u"boolean"_s, u"Brief."_s)}})},
        [](const QJsonObject&) -> Meow2::Result<Meow2::Editor::CommandOutcome>
        {
            return Meow2::Editor::CommandOutcome{
                .result = QJsonObject{{u"captured"_s, true},
                                      {u"pending"_s, false},
                                      {u"frame"_s, 42},
                                      {u"flow"_s, Meow2::Editor::flowToJson(smallFlow())},
                                      {u"snapshots"_s, QJsonArray{QJsonObject{{u"pass"_s, 1}, {u"resource"_s, 0}}}}}};
        }));
    ASSERT_TRUE(m_commands.add(
        {.name = u"capture.show"_s,
         .description = u"Stand-in."_s,
         .parameters = Meow2::Editor::objectSchema(
             {{.name = u"pass"_s, .schema = Meow2::Editor::propertySchema(u"integer"_s, u"Pass."_s)},
              {.name = u"resource"_s, .schema = Meow2::Editor::propertySchema(u"integer"_s, u"Resource."_s)},
              {.name = u"channels"_s, .schema = Meow2::Editor::propertySchema(u"integer"_s, u"Channels."_s)},
              {.name = u"rangeMin"_s, .schema = Meow2::Editor::propertySchema(u"number"_s, u"Min."_s)},
              {.name = u"rangeMax"_s, .schema = Meow2::Editor::propertySchema(u"number"_s, u"Max."_s)}})},
        [&shown](const QJsonObject& parameters) -> Meow2::Result<Meow2::Editor::CommandOutcome>
        {
            shown = parameters;
            return Meow2::Editor::CommandOutcome{};
        }));
    ASSERT_TRUE(m_commands.add(
        {.name = u"capture.image"_s,
         .description = u"Stand-in."_s,
         .kind = Meow2::Editor::CommandKind::Query,
         .parameters = Meow2::Editor::objectSchema(
             {{.name = u"data"_s, .schema = Meow2::Editor::propertySchema(u"boolean"_s, u"Data."_s)}})},
        [&serial](const QJsonObject& parameters) -> Meow2::Result<Meow2::Editor::CommandOutcome>
        {
            QJsonObject result{{u"serial"_s, serial}, {u"ready"_s, serial > 0.0}, {u"pending"_s, false}};
            if (serial > 0.0 && parameters.value(u"data").toBool())
            {
                const std::vector<std::byte> white(16, std::byte{255});
                const Meow2::Result<std::vector<std::byte>> png =
                    Meow2::Assets::encodePng({.width = 2, .height = 2, .pixels = white});
                result.insert(u"png", QString::fromLatin1(QByteArray(reinterpret_cast<const char*>(png->data()),
                                                                     static_cast<qsizetype>(png->size()))
                                                              .toBase64()));
            }
            return Meow2::Editor::CommandOutcome{.result = result};
        }));

    Meow2::Editor::RenderGraphDock dock{m_commands, m_log};
    dock.resize(800, 600);
    dock.refresh();
    Meow2::Editor::FlowView& view = dock.flowView();
    ASSERT_EQ(view.geometry().nodes.size(), 3U) << "a node per pass";
    EXPECT_TRUE(dock.findChild<QLabel*>(u"CaptureStatus"_s)->text().contains(u"Frame 42"_s));

    // An input pin on Tone map: its colour as Shade (the pass before) left it, through capture.show.
    const Renderer::FlowPin colourIn{.pass = 2, .resource = 0, .output = false};
    const std::optional<QPointF> at = view.pinPosition(colourIn);
    ASSERT_TRUE(at);
    QMouseEvent press{QEvent::MouseButtonPress, *at,           view.mapToGlobal(*at), Qt::LeftButton,
                      Qt::LeftButton,           Qt::NoModifier};
    QApplication::sendEvent(&view, &press);
    ASSERT_TRUE(shown) << "clicking the pin asked for the texture";
    EXPECT_EQ(shown->value(u"pass").toInt(), 1) << "the pass that wrote it";
    EXPECT_EQ(shown->value(u"resource").toInt(), 0);
    EXPECT_EQ(shown->value(u"channels").toInt(), 0b0111) << "colour as it is";
    EXPECT_EQ(dock.findChild<QListWidget*>(u"PassAccesses"_s)->count(), 1) << "Tone map's one read is listed";

    // A buffer pin has no picture.
    shown.reset();
    dock.selectPin({.pass = 1, .resource = 1, .output = false});
    EXPECT_FALSE(shown);
    EXPECT_EQ(dock.findChild<QListWidget*>(u"PassAccesses"_s)->count(), 2);

    // The picture comes back: shown in the preview.
    serial = 1.0;
    dock.refresh();
    const QLabel* preview = dock.findChild<QLabel*>(u"CapturePreview"_s);
    ASSERT_NE(preview, nullptr);
    EXPECT_FALSE(preview->pixmap().isNull());
}
