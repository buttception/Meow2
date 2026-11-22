/// @file
/// @brief DebugTools (DebugTools.h).

#include "Viewports/DebugTools.h"

#include "Commands/CommandSchema.h"
#include "Viewports/EditorRenderer.h"
#include "Viewports/EditorWorld.h"

#include <Meow2/Core/Memory/GlobalTracking.h>
#include <Meow2/Core/Memory/MemTag.h>
#include <Meow2/Core/Memory/TrackingAllocator.h>

#include <QJsonArray>
#include <string>

namespace Meow2::Editor
{
using namespace Qt::StringLiterals;

namespace
{
/// @brief A string as a QString.
QString toQString(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}
} // namespace

DebugTools::DebugTools(const DebugToolsDeps& deps)
    : m_deps(deps)
{
    // The saved choices; a hand-edited file with a name this build doesn't know falls back to the default.
    const Config::Config& config = m_deps.settings.config();
    const std::string view = config.getString(c_debugViewKey, "shaded");
    if (const Renderer::DebugViewEntry* entry = Renderer::findDebugView(std::string_view{view}); entry != nullptr)
    {
        m_state.view = entry->view;
    }
    m_state.wireframe = Renderer::wireframeModeFromName(config.getString(c_debugWireframeKey, "off"))
                            .value_or(Renderer::WireframeMode::Off);
    m_state.showFrustum = config.getBool(c_debugShowFrustumKey, true);
    m_grid = config.getBool(c_debugGridKey, false);
    registerCommands();
}

DebugTools::~DebugTools()
{
    for (const auto* name : {u"debug.setView", u"debug.setWireframe", u"debug.setCulling", u"debug.setGrid",
                             u"debug.state", u"stats.frame"})
    {
        m_deps.commands.remove(QStringView{name});
    }
}

void DebugTools::addFrame(float seconds)
{
    m_frames.add(seconds * 1000.0F);
}

void DebugTools::apply()
{
    if (m_deps.renderer != nullptr)
    {
        m_deps.renderer->debugState() = m_state;
        m_deps.renderer->setGrid(m_grid);
    }
}

void DebugTools::save()
{
    Config::Config& config = m_deps.settings.config();
    const Renderer::DebugViewEntry* entry = Renderer::findDebugView(m_state.view);
    config.setString(c_debugViewKey, entry != nullptr ? entry->key : std::string_view{"shaded"});
    config.setString(c_debugWireframeKey, Renderer::wireframeModeName(m_state.wireframe));
    config.setBool(c_debugShowFrustumKey, m_state.showFrustum);
    config.setBool(c_debugGridKey, m_grid);
    if (Result<void> saved = config.save(); !saved)
    {
        MEOW2_LOG_WARN(m_deps.log, "Debug tool settings not saved: {}", saved.error());
    }
}

QJsonObject DebugTools::stateJson() const
{
    const Renderer::DebugViewEntry* entry = Renderer::findDebugView(m_state.view);
    return QJsonObject{
        {u"view"_s, toQString(entry != nullptr ? entry->key : std::string_view{"shaded"})},
        {u"wireframe"_s, toQString(Renderer::wireframeModeName(m_state.wireframe))},
        {u"freezeCulling"_s, m_state.freezeCulling},
        {u"showFrustum"_s, m_state.showFrustum},
        {u"grid"_s, m_grid},
        {u"failed"_s, m_deps.renderer != nullptr && m_deps.renderer->debugFailed()},
    };
}

QJsonObject DebugTools::statsJson() const
{
    // JSON numbers are doubles: byte counts stay exact far past any GPU's memory (2^53 bytes).
    const auto number = [](auto value)
    {
        return QJsonValue{static_cast<double>(value)};
    };
    QJsonArray frames;
    const std::array<float, Time::FrameTimes::c_capacity> times = m_frames.ordered();
    for (std::size_t i = times.size() - m_frames.count(); i < times.size(); ++i)
    {
        frames.append(number(times[i]));
    }
    const float average = m_frames.average();
    QJsonObject stats{
        {u"frameMs"_s, number(average)},
        {u"fps"_s, number(average > 0.0F ? 1000.0F / average : 0.0F)},
        {u"worstMs"_s, number(m_frames.worst())},
        {u"frames"_s, frames},
        {u"entities"_s, number(m_deps.world.scene().entityCount())},
        {u"cpuMemoryTracked"_s, Memory::c_memoryTrackingAvailable},
    };
    Rhi::GpuMemoryStats gpuMemory{};
    if (m_deps.renderer != nullptr)
    {
        const RenderStats render = m_deps.renderer->stats();
        gpuMemory = render.gpuMemory;
        stats.insert(u"backend", toQString(render.backend));
        stats.insert(u"gpu", toQString(render.gpu));
        stats.insert(u"gpuMemoryUsed", number(render.gpuMemory.usedBytes));
        stats.insert(u"gpuMemoryBudget", number(render.gpuMemory.budgetBytes));
        stats.insert(u"validationErrors", number(render.validationErrors));
        stats.insert(u"instances", number(render.instances));
        stats.insert(u"meshes", number(render.meshes));
        stats.insert(u"materials", number(render.materials));
        stats.insert(u"textures", number(render.textures));
        stats.insert(u"drawPath", render.meshlets ? u"meshlets"_s : u"indirect"_s);
    }
    QJsonArray memory;
    for (std::size_t i = 0; i < Memory::c_memTagCount; ++i)
    {
        const auto tag = static_cast<Memory::MemTag>(i);
        const std::size_t cpu = Memory::globalTracker().stats(tag).currentBytes;
        const std::uint64_t gpu = gpuMemory.bytesByTag[i];
        if (cpu == 0 && gpu == 0)
        {
            continue; // tags nothing has used: a shorter table
        }
        memory.append(QJsonObject{{u"tag"_s, toQString(Memory::memTagName(tag))},
                                  {u"cpuBytes"_s, number(cpu)},
                                  {u"gpuBytes"_s, number(gpu)}});
    }
    stats.insert(u"memory", memory);
    return stats;
}

void DebugTools::registerCommands()
{
    EditorCommandRegistry& commands = m_deps.commands;

    QJsonArray viewKeys;
    for (const Renderer::DebugViewEntry& entry : Renderer::debugViewEntries())
    {
        viewKeys.append(toQString(entry.key));
    }
    QJsonObject view = propertySchema(u"string"_s, u"The view: shaded, or a debug view (normals, overdraw, …)."_s);
    view.insert(u"enum", viewKeys);
    [[maybe_unused]] const Result<void> setView = commands.add(
        {.name = u"debug.setView"_s,
         .description = u"What the editor viewport shows: the shaded scene or a debug view. Not undoable; saved."_s,
         .parameters = objectSchema({{.name = u"view"_s, .schema = view, .required = true}})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            // The schema's enum already refused unknown keys.
            const Renderer::DebugViewEntry* entry =
                Renderer::findDebugView(std::string_view{parameters.value(u"view").toString().toStdString()});
            m_state.view = entry->view;
            save();
            return CommandOutcome{.result = stateJson()};
        });

    QJsonObject mode = propertySchema(
        u"string"_s, u"off: none; overlay: edges over the scene; only: edges on black, hidden lines removed."_s);
    mode.insert(u"enum", QJsonArray{u"off"_s, u"overlay"_s, u"only"_s});
    [[maybe_unused]] const Result<void> setWireframe =
        commands.add({.name = u"debug.setWireframe"_s,
                      .description = u"Wireframe in the editor viewport. Not undoable; saved."_s,
                      .parameters = objectSchema({{.name = u"mode"_s, .schema = mode, .required = true}})},
                     [this](const QJsonObject& parameters) -> Result<CommandOutcome>
                     {
                         m_state.wireframe =
                             *Renderer::wireframeModeFromName(parameters.value(u"mode").toString().toStdString());
                         save();
                         return CommandOutcome{.result = stateJson()};
                     });

    [[maybe_unused]] const Result<void> setCulling = commands.add(
        {.name = u"debug.setCulling"_s,
         .description = u"Freeze culling at the editor camera's view of the moment (fly away and see what was culled), "
                        u"and whether to draw that frozen view. Not undoable."_s,
         .parameters = objectSchema(
             {{.name = u"freeze"_s,
               .schema = propertySchema(u"boolean"_s, u"Keep culling with the camera of the moment."_s)},
              {.name = u"showFrustum"_s,
               .schema = propertySchema(u"boolean"_s, u"While frozen, draw the frozen view volume (saved)."_s)}})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            if (parameters.contains(u"freeze"))
            {
                m_state.freezeCulling = parameters.value(u"freeze").toBool();
            }
            if (parameters.contains(u"showFrustum"))
            {
                m_state.showFrustum = parameters.value(u"showFrustum").toBool();
                save();
            }
            return CommandOutcome{.result = stateJson()};
        });

    [[maybe_unused]] const Result<void> setGrid = commands.add(
        {.name = u"debug.setGrid"_s,
         .description =
             u"The ground grid (y = 0, the X axis red, Z blue) in the editor viewport. Not undoable; saved."_s,
         .parameters = objectSchema(
             {{.name = u"on"_s, .schema = propertySchema(u"boolean"_s, u"Draw the grid."_s), .required = true}})},
        [this](const QJsonObject& parameters) -> Result<CommandOutcome>
        {
            m_grid = parameters.value(u"on").toBool();
            save();
            return CommandOutcome{.result = stateJson()};
        });

    [[maybe_unused]] const Result<void> state = commands.add(
        {.name = u"debug.state"_s,
         .description = u"The editor viewport's debug tools: view, wireframe, culling, grid."_s,
         .kind = CommandKind::Query},
        [this](const QJsonObject&) -> Result<CommandOutcome> { return CommandOutcome{.result = stateJson()}; });

    [[maybe_unused]] const Result<void> stats = commands.add(
        {.name = u"stats.frame"_s,
         .description = u"Frame times (the last few seconds), the GPU and its memory, the scene's counts, and memory "
                        u"per tag (CPU and GPU)."_s,
         .kind = CommandKind::Query},
        [this](const QJsonObject&) -> Result<CommandOutcome> { return CommandOutcome{.result = statsJson()}; });
}
} // namespace Meow2::Editor
