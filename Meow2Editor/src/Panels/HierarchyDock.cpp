/// @file
/// @brief HierarchyDock (HierarchyDock.h).

#include "Panels/HierarchyDock.h"

#include <functional>
#include <QDropEvent>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>
#include <QVBoxLayout>

namespace Meow2::Editor
{
using namespace Qt::StringLiterals;

namespace
{
/// @brief The entity ID an item stands for.
constexpr int c_idRole = Qt::UserRole;

/// @brief A tree whose drops become commands: Qt's own move would change the tree but not the scene.
class HierarchyTree final : public QTreeWidget
{
public:
    explicit HierarchyTree(QWidget* parent)
        : QTreeWidget(parent)
    {
    }

    std::function<void(const QStringList&, const QString&)> onDrop; ///< Dragged IDs and the target's (empty: root).

protected:
    /// @brief Does nothing: Qt's trees jump to the item whose name starts with a typed letter, so W/E/R or F with
    ///        the Hierarchy focused selected some entity "W…" (owner report). Keys here are for shortcuts.
    void keyboardSearch(const QString&) override {}

    void dropEvent(QDropEvent* event) override
    {
        QStringList ids;
        for (const QTreeWidgetItem* item : selectedItems())
        {
            ids.push_back(item->data(0, c_idRole).toString());
        }
        const QTreeWidgetItem* target = itemAt(event->position().toPoint());
        // The tree is rebuilt from the scene after the command; Qt mustn't move the items itself.
        event->setDropAction(Qt::IgnoreAction);
        event->accept();
        if (onDrop && !ids.isEmpty())
        {
            onDrop(ids, target != nullptr ? target->data(0, c_idRole).toString() : QString{});
        }
    }
};
} // namespace

HierarchyDock::HierarchyDock(EditorCommandRegistry& commands, const SceneEditor& editor, const EditorWorld& world,
                             Log::Logger& log, QWidget* parent)
    : QDockWidget(u"Hierarchy"_s, parent)
    , m_commands(commands)
    , m_editor(editor)
    , m_world(world)
    , m_log(log)
{
    setObjectName(u"HierarchyDock"_s); // QMainWindow::saveState() keys docks by object name
    auto* content = new QWidget(this); // Qt parent–child ownership throughout
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto* buttons = new QHBoxLayout();
    buttons->setContentsMargins(6, 4, 6, 4);
    auto* create = new QPushButton(u"Create"_s, content);
    create->setObjectName(u"CreateEntity"_s);
    create->setToolTip(u"A new entity, under the selected one (or at the root)."_s);
    buttons->addWidget(create);
    buttons->addStretch(1);
    layout->addLayout(buttons);

    auto* tree = new HierarchyTree(content);
    m_tree = tree;
    m_tree->setObjectName(u"HierarchyTree"_s);
    m_tree->setHeaderHidden(true);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setDragDropMode(QAbstractItemView::InternalMove);
    // F2 renames; a double-click frames the entity in the editor viewport (as in Unity), so it doesn't rename too.
    m_tree->setEditTriggers(QAbstractItemView::EditKeyPressed);
    tree->onDrop = [this](const QStringList& ids, const QString& target)
    {
        reparent(ids, target);
    };
    layout->addWidget(m_tree, 1);
    setWidget(content);

    connect(m_tree, &QTreeWidget::itemSelectionChanged, this,
            [this]
            {
                if (m_updating)
                {
                    return;
                }
                QJsonArray ids;
                for (const QTreeWidgetItem* item : m_tree->selectedItems())
                {
                    ids.append(item->data(0, c_idRole).toString());
                }
                run(u"selection.set"_s, {{u"ids"_s, ids}});
            });
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem* item, int)
            { run(u"viewport.focus"_s, {{u"id"_s, item->data(0, c_idRole).toString()}}); });
    connect(m_tree, &QTreeWidget::itemChanged, this,
            [this](QTreeWidgetItem* item, int)
            {
                if (!m_updating)
                {
                    run(u"entity.rename"_s,
                        {{u"id"_s, item->data(0, c_idRole).toString()}, {u"name"_s, item->text(0)}});
                }
            });
    connect(create, &QPushButton::clicked, this,
            [this]
            {
                const std::vector<EntityId> selected = m_editor.selection();
                run(u"entity.create"_s,
                    {{u"name"_s, u"Entity"_s},
                     {u"parent"_s,
                      selected.empty() ? QString{} : QString::fromStdString(selected.front().uuid().toString())}});
            });
}

void HierarchyDock::run(const QString& command, const QJsonObject& parameters)
{
    if (const Result<QJsonValue> ran = m_commands.execute(command, parameters); !ran)
    {
        MEOW2_LOG_WARN(m_log, "{}: {}", command.toStdString(), ran.error());
    }
}

void HierarchyDock::reparent(const QStringList& ids, const QString& parent)
{
    for (const QString& id : ids)
    {
        if (id != parent)
        {
            run(u"entity.setParent"_s, {{u"id"_s, id}, {u"parent"_s, parent}});
        }
    }
}

void HierarchyDock::addItems(const QJsonObject& entity, QTreeWidgetItem* parent)
{
    auto* item = parent != nullptr ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
    const QString name = entity.value(u"name").toString();
    item->setText(0, name.isEmpty() ? u"(unnamed)"_s : name);
    item->setData(0, c_idRole, entity.value(u"id").toString());
    item->setFlags(item->flags() | Qt::ItemIsEditable | Qt::ItemIsDragEnabled | Qt::ItemIsDropEnabled);
    for (const QJsonValue& child : entity.value(u"children").toArray())
    {
        addItems(child.toObject(), item);
    }
}

void HierarchyDock::showSelection()
{
    QStringList wanted;
    for (const EntityId id : m_editor.selection())
    {
        wanted.push_back(QString::fromStdString(id.uuid().toString()));
    }
    for (QTreeWidgetItemIterator it{m_tree}; *it != nullptr; ++it)
    {
        const bool selected = wanted.contains((*it)->data(0, c_idRole).toString());
        if ((*it)->isSelected() != selected)
        {
            (*it)->setSelected(selected);
        }
    }
}

void HierarchyDock::refresh()
{
    m_updating = true;
    if (m_world.revision() != m_shownRevision)
    {
        m_shownRevision = m_world.revision();
        const Result<QJsonValue> tree = m_commands.execute(u"scene.hierarchy");
        m_tree->clear();
        if (tree)
        {
            for (const QJsonValue& entity : tree->toObject().value(u"entities").toArray())
            {
                addItems(entity.toObject(), nullptr);
            }
        }
        m_tree->expandAll();
    }
    showSelection(); // the selection can change without the scene (a click in the viewport, an agent)
    m_updating = false;
}
} // namespace Meow2::Editor
