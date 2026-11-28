/// @file
/// @brief PlayMode (PlayMode.h).

#include "Editing/PlayMode.h"

#include <Meow2/Graphics/Renderer/SceneModel.h>

#include <QJsonArray>
#include <QJsonObject>

namespace Meow2::Editor
{
using namespace Qt::StringLiterals;

PlayMode::PlayMode(const PlayModeDeps& deps)
    : m_deps(deps)
{
    registerCommands();
}

PlayMode::~PlayMode()
{
    if (playing())
    {
        static_cast<void>(stop());
    }
    for (const auto* name : {u"play.start", u"play.stop", u"play.pause", u"play.resume", u"play.state"})
    {
        m_deps.commands.remove(QStringView{name});
    }
}

Result<void> PlayMode::start()
{
    if (playing())
    {
        return fail(ErrorCode::InvalidArgument, "already playing");
    }
    if (m_deps.physics == nullptr)
    {
        return fail(ErrorCode::Unsupported, "this editor runs without physics");
    }
    Result<std::string> snapshot = m_deps.world.snapshot();
    if (!snapshot)
    {
        return Unexpected<Error>{snapshot.error()};
    }
    Result<std::unique_ptr<Physics::PhysicsWorld>> world =
        Physics::PhysicsWorld::create({.library = *m_deps.physics, .jobs = m_deps.jobs, .log = m_deps.log}, {});
    if (!world)
    {
        return Unexpected<Error>{world.error()};
    }
    m_snapshot = std::move(*snapshot);
    m_physicsWorld = std::move(*world);
    m_physicsScene = std::make_unique<Physics::PhysicsScene>(Physics::PhysicsSceneDeps{
        .scene = m_deps.world.scene(),
        .transforms = m_deps.world.hierarchy(),
        .world = *m_physicsWorld,
        .meshes = m_deps.world.collisionMeshes(),
        // A mesh Collider uses what the entity shows (the renderer's MeshRenderer: Physics doesn't know about it).
        .meshOf = [](const Scene::Scene& scene, entt::entity entity) -> std::optional<AssetId>
        {
            const auto* renderer = scene.registry().try_get<Renderer::MeshRenderer>(entity);
            return renderer != nullptr ? std::optional<AssetId>{renderer->mesh} : std::nullopt;
        },
        .log = m_deps.log});
    m_warnings = m_physicsScene->build();
    m_seconds = 0.0;
    m_paused = false;
    m_deps.history.setPaused(true);
    m_deps.world.setPlaying(true);
    MEOW2_LOG_INFO(m_deps.log, "Play: {} bodies", m_physicsScene->bodyCount());
    return {};
}

Result<void> PlayMode::stop()
{
    if (!playing())
    {
        return fail(ErrorCode::InvalidArgument, "not playing");
    }
    // The bodies point into the scene that's about to be replaced: they go first.
    m_physicsScene.reset();
    m_physicsWorld.reset();
    m_paused = false;
    m_deps.world.setPlaying(false);
    m_deps.history.setPaused(false);
    if (Result<void> restored = m_deps.world.restore(m_snapshot); !restored)
    {
        MEOW2_LOG_ERROR(m_deps.log, "Stop: the scene from before Play can't be read back; keeping the played one: {}",
                        restored.error());
        return restored;
    }
    m_snapshot.clear();
    MEOW2_LOG_INFO(m_deps.log, "Stop: the scene is back as it was ({:.1f} s played)", m_seconds);
    return {};
}

Result<void> PlayMode::setPaused(bool paused)
{
    if (!playing())
    {
        return fail(ErrorCode::InvalidArgument, "not playing");
    }
    if (m_paused != paused)
    {
        m_paused = paused;
        MEOW2_LOG_INFO(m_deps.log, "Play: {} at {:.1f} s", paused ? "paused" : "resumed", m_seconds);
    }
    return {};
}

void PlayMode::fixedUpdate(float seconds)
{
    if (!playing() || m_paused)
    {
        return;
    }
    m_physicsScene->step(seconds);
    m_seconds += static_cast<double>(seconds);
    // No markChanged(): the docks would rebuild every step. The viewports gather transforms every frame anyway.
}

void PlayMode::registerCommands()
{
    EditorCommandRegistry& commands = m_deps.commands;
    [[maybe_unused]] const Result<void> started = commands.add(
        {.name = u"play.start"_s,
         .description = u"Plays the scene: its RigidBody and Collider components simulate until play.stop, which "
                        u"puts the scene back as it was. Undo and the scene file commands are off meanwhile."_s,
         .isEnabled =
             [this]
         {
             return !playing();
         }},
        [this](const QJsonObject&) -> Result<CommandOutcome>
        {
            if (Result<void> result = start(); !result)
            {
                return Unexpected<Error>{result.error()};
            }
            QJsonArray warnings;
            for (const std::string& warning : m_warnings)
            {
                warnings.append(QString::fromStdString(warning));
            }
            return CommandOutcome{.result = QJsonObject{{u"bodies"_s, static_cast<qint64>(m_physicsScene->bodyCount())},
                                                        {u"warnings"_s, warnings}}};
        });
    [[maybe_unused]] const Result<void> stopped =
        commands.add({.name = u"play.stop"_s,
                      .description = u"Stops playing and puts the scene back as it was before play.start."_s,
                      .isEnabled =
                          [this]
                      {
                          return playing();
                      }},
                     [this](const QJsonObject&) -> Result<CommandOutcome>
                     {
                         if (Result<void> result = stop(); !result)
                         {
                             return Unexpected<Error>{result.error()};
                         }
                         return CommandOutcome{};
                     });
    // Pause and resume as two commands, not a toggle: an agent saying "pause" twice gets a paused scene.
    const auto pauseCommand = [this, &commands](QString name, QString description, bool pause)
    {
        [[maybe_unused]] const Result<void> added =
            commands.add({.name = std::move(name),
                          .description = std::move(description),
                          .isEnabled =
                              [this, pause]
                          {
                              return playing() && paused() != pause;
                          }},
                         [this, pause](const QJsonObject&) -> Result<CommandOutcome>
                         {
                             if (Result<void> result = setPaused(pause); !result)
                             {
                                 return Unexpected<Error>{result.error()};
                             }
                             return CommandOutcome{};
                         });
    };
    pauseCommand(u"play.pause"_s,
                 u"Pauses playing: the physics stops and the scene stays as it is until play.resume (play.stop still "
                 u"puts it back as it was before play.start)."_s,
                 true);
    pauseCommand(u"play.resume"_s, u"Resumes playing after play.pause."_s, false);
    [[maybe_unused]] const Result<void> state = commands.add(
        {.name = u"play.state"_s,
         .description =
             u"Whether the scene is playing and paused, for how many seconds, how many physics bodies it has, and "
             u"what couldn't be simulated."_s,
         .kind = CommandKind::Query},
        [this](const QJsonObject&) -> Result<CommandOutcome>
        {
            QJsonArray warnings;
            for (const std::string& warning : m_warnings)
            {
                warnings.append(QString::fromStdString(warning));
            }
            return CommandOutcome{
                .result =
                    QJsonObject{{u"playing"_s, playing()},
                                {u"paused"_s, paused()},
                                {u"seconds"_s, m_seconds},
                                {u"bodies"_s, playing() ? static_cast<qint64>(m_physicsScene->bodyCount()) : qint64{0}},
                                {u"warnings"_s, warnings}}};
        });
}
} // namespace Meow2::Editor
