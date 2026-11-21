/// @file
/// @brief InspectorDock (InspectorDock.h).

#include "Panels/InspectorDock.h"

#include "Panels/FieldWidgets.h"

#include <Meow2/Math/Quaternion.h>
#include <Meow2/Math/Scalar.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QDoubleSpinBox>
#include <QEvent>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>
#include <vector>

namespace Meow2::Editor
{
using namespace Qt::StringLiterals;

namespace
{
/// @brief Whether two rotations are the same (q and −q are).
bool sameRotation(const Math::Quat& a, const Math::Quat& b)
{
    return std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w) > 0.99999F;
}
} // namespace

InspectorDock::InspectorDock(EditorCommandRegistry& commands, const SceneEditor& editor, const EditorWorld& world,
                             Log::Logger& log, QWidget* parent)
    : QDockWidget(u"Inspector"_s, parent)
    , m_commands(commands)
    , m_editor(editor)
    , m_world(world)
    , m_log(log)
{
    setObjectName(u"InspectorDock"_s); // QMainWindow::saveState() keys docks by object name
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setObjectName(u"InspectorScroll"_s);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff); // the panel is kept wide enough instead (build())
    setWidget(scroll);
    // Components as cards: a dark frame each, a header bar with the theme's red at its left edge.
    setStyleSheet(
        u"QFrame[componentCard=\"true\"] { border: 1px solid #2A2A30; border-radius: 4px; background: #08080A; }"
        u"QWidget[componentHeader=\"true\"] { background: #17171B; border-left: 3px solid #E01E2D; }"_s);
}

bool InspectorDock::run(const QString& command, const QJsonObject& parameters)
{
    const Result<QJsonValue> ran = m_commands.execute(command, parameters);
    if (!ran)
    {
        MEOW2_LOG_WARN(m_log, "{}: {}", command.toStdString(), ran.error());
    }
    return ran.has_value();
}

void InspectorDock::setField(const QString& component, const QString& field, const QJsonValue& value)
{
    if (m_shown)
    {
        run(u"component.setField"_s, {{u"id"_s, QString::fromStdString(m_shown->uuid().toString())},
                                      {u"component"_s, component},
                                      {u"field"_s, field},
                                      {u"value"_s, value}});
    }
}

void InspectorDock::refresh()
{
    const std::vector<EntityId> selection = m_editor.selection();
    const std::optional<EntityId> target = selection.size() == 1 ? std::optional{selection.front()} : std::nullopt;
    if (m_built && target == m_shown && m_world.revision() == m_shownRevision)
    {
        return;
    }
    m_shown = target;
    m_shownRevision = m_world.revision();
    m_built = true;
    build();
}

void InspectorDock::build()
{
    auto* scroll = static_cast<QScrollArea*>(widget());
    m_content = new QWidget();
    m_layout = new QVBoxLayout(m_content);
    m_layout->setContentsMargins(6, 6, 6, 6);
    scroll->setWidget(m_content); // deletes the previous editors
    if (!m_shown)
    {
        m_layout->addWidget(new QLabel(u"Select one entity to see its components."_s, m_content));
        m_layout->addStretch(1);
        return;
    }
    const QString id = QString::fromStdString(m_shown->uuid().toString());
    const Result<QJsonValue> entity = m_commands.execute(u"entity.get", {{u"id"_s, id}});
    const Result<QJsonValue> list = m_commands.execute(u"component.list");
    if (!entity || !list)
    {
        m_layout->addWidget(new QLabel(u"The entity can't be read."_s, m_content));
        return;
    }
    const QJsonObject info = entity->toObject();

    auto* name = new QLineEdit(info.value(u"name").toString(), m_content);
    name->setObjectName(u"Name"_s);
    name->setPlaceholderText(u"Name"_s);
    connect(name, &QLineEdit::editingFinished, this,
            [this, name, id, before = info.value(u"name").toString()]() mutable
            {
                if (name->text() != before)
                {
                    before = name->text();
                    run(u"entity.rename"_s, {{u"id"_s, id}, {u"name"_s, name->text()}});
                }
            });
    m_layout->addWidget(name);
    // The ID in grey under the name: what scene files, commands and agents call the entity. Selectable, to copy.
    auto* idLabel = new QLabel(id, m_content);
    idLabel->setObjectName(u"EntityId"_s);
    idLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    idLabel->setStyleSheet(u"color: #6E6E76; font-size: 11px;"_s);
    m_layout->addWidget(idLabel);
    m_layout->addSpacing(6);

    // Field types by component, from the registry's descriptions.
    std::map<QString, QJsonArray> fieldsOf;
    QStringList addable;
    QStringList present;
    for (const QJsonValue& component : info.value(u"components").toArray())
    {
        present.push_back(component.toObject().value(u"name").toString());
    }
    for (const QJsonValue& component : list->toObject().value(u"components").toArray())
    {
        const QString componentName = component.toObject().value(u"name").toString();
        fieldsOf[componentName] = component.toObject().value(u"fields").toArray();
        if (!present.contains(componentName) && componentName != u"Parent"_s)
        {
            addable.push_back(componentName);
        }
    }

    // Each component a card: a header bar (a fold arrow, the name, Remove) over its fields, with space between cards,
    // so where one ends and the next begins is plain at a glance.
    for (const QJsonValue& entry : info.value(u"components").toArray())
    {
        const QJsonObject component = entry.toObject();
        const QString componentName = component.value(u"name").toString();
        auto* card = new QFrame(m_content);
        card->setObjectName(componentName);
        card->setProperty("componentCard", true);
        auto* cardLayout = new QVBoxLayout(card);
        cardLayout->setContentsMargins(0, 0, 0, 0);
        cardLayout->setSpacing(0);

        auto* header = new QWidget(card);
        header->setProperty("componentHeader", true);
        header->setAttribute(Qt::WA_StyledBackground); // so the style sheet's header colour is drawn
        auto* headerLayout = new QHBoxLayout(header);
        headerLayout->setContentsMargins(4, 2, 4, 2);
        auto* fold = new QToolButton(header);
        fold->setObjectName(u"Fold."_s + componentName);
        fold->setAutoRaise(true);
        fold->setCheckable(true);
        fold->setChecked(!m_folded.contains(componentName));
        fold->setArrowType(fold->isChecked() ? Qt::DownArrow : Qt::RightArrow);
        headerLayout->addWidget(fold);
        auto* title = new QLabel(componentName, header);
        title->setStyleSheet(u"font-weight: 600;"_s);
        headerLayout->addWidget(title, 1);
        if (componentName != u"Parent"_s)
        {
            auto* remove = new QToolButton(header);
            remove->setObjectName(u"Remove."_s + componentName);
            remove->setText(u"Remove"_s);
            remove->setAutoRaise(true);
            remove->setToolTip(u"Remove %1"_s.arg(componentName));
            connect(remove, &QToolButton::clicked, this, [this, id, componentName]
                    { run(u"component.remove"_s, {{u"id"_s, id}, {u"component"_s, componentName}}); });
            headerLayout->addWidget(remove);
        }
        cardLayout->addWidget(header);

        auto* body = new QWidget(card);
        auto* form = new QFormLayout(body);
        form->setContentsMargins(8, 4, 6, 6);
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        const QJsonObject values = component.value(u"fields").toObject();
        for (const QJsonValue& field : fieldsOf[componentName])
        {
            const QJsonObject description = field.toObject();
            const QString fieldName = description.value(u"name").toString();
            form->addRow(fieldName, fieldEditor(componentName, description, values.value(fieldName), body));
        }
        body->setVisible(fold->isChecked());
        cardLayout->addWidget(body);
        // Folding is how the panel is viewed, not an edit: remembered per component type while the editor runs.
        connect(fold, &QToolButton::toggled, this,
                [this, fold, body, componentName](bool open)
                {
                    fold->setArrowType(open ? Qt::DownArrow : Qt::RightArrow);
                    body->setVisible(open);
                    if (open)
                    {
                        m_folded.remove(componentName);
                    }
                    else
                    {
                        m_folded.insert(componentName);
                    }
                });
        m_layout->addWidget(card);
        m_layout->addSpacing(6);
    }
    if (const QJsonArray unknown = info.value(u"unknown").toArray(); !unknown.isEmpty())
    {
        QStringList names;
        for (const QJsonValue& value : unknown)
        {
            names.push_back(value.toString());
        }
        auto* kept = new QLabel(u"Kept from the scene file (not known here): "_s + names.join(u", "_s), m_content);
        kept->setWordWrap(true);
        m_layout->addWidget(kept);
    }
    if (!addable.isEmpty())
    {
        // A search field rather than a list to scroll: with scripts there will be many components to add.
        auto* add = new QLineEdit(m_content);
        add->setObjectName(u"AddComponent"_s);
        add->setPlaceholderText(u"Add component… (type to search)"_s);
        add->setClearButtonEnabled(true);
        auto* completer = new QCompleter(addable, add);
        completer->setCaseSensitivity(Qt::CaseInsensitive);
        completer->setFilterMode(Qt::MatchContains);
        completer->setCompletionMode(QCompleter::PopupCompletion);
        add->setCompleter(completer);
        const auto addNamed = [this, id, addable](const QString& typed)
        {
            const auto found = std::ranges::find_if(addable, [&typed](const QString& candidate)
                                                    { return candidate.compare(typed, Qt::CaseInsensitive) == 0; });
            if (found != addable.end())
            {
                run(u"component.add"_s, {{u"id"_s, id}, {u"component"_s, *found}});
            }
        };
        connect(completer, qOverload<const QString&>(&QCompleter::activated), this, addNamed);
        connect(add, &QLineEdit::returnPressed, this, [add, addNamed] { addNamed(add->text().trimmed()); });
        // The whole list on a click into the empty field, so there's something to pick without knowing names.
        add->installEventFilter(this);
        m_layout->addWidget(add);
    }
    m_layout->addStretch(1);

    // Wide enough for the fields and the scroll bar beside them: the panel can't be squeezed until the scroll bar
    // covers the boxes.
    const int needed = m_content->minimumSizeHint().width() + scroll->verticalScrollBar()->sizeHint().width() +
                       2 * scroll->frameWidth();
    scroll->setMinimumWidth(needed);
}

bool InspectorDock::eventFilter(QObject* watched, QEvent* event)
{
    auto* add = qobject_cast<QLineEdit*>(watched);
    if (add != nullptr && add->objectName() == u"AddComponent"_s && event->type() == QEvent::MouseButtonPress &&
        add->text().isEmpty() && add->completer() != nullptr)
    {
        add->completer()->setCompletionPrefix(QString{});
        add->completer()->complete();
    }
    return QDockWidget::eventFilter(watched, event);
}

QWidget* InspectorDock::fieldEditor(const QString& component, const QJsonObject& field, const QJsonValue& value,
                                    QWidget* parent)
{
    const QString fieldName = field.value(u"name").toString();
    const QString type = field.value(u"type").toString();
    const QString objectName = component + u'.' + fieldName;

    if (type == u"bool"_s)
    {
        auto* box = new QCheckBox(parent);
        box->setObjectName(objectName);
        box->setChecked(value.toBool());
        connect(box, &QCheckBox::toggled, this,
                [this, component, fieldName](bool on) { setField(component, fieldName, on); });
        return box;
    }
    if (type == u"int"_s)
    {
        auto* box = new CompactBox<QSpinBox>(parent);
        box->setButtonSymbols(QAbstractSpinBox::NoButtons);
        box->setObjectName(objectName);
        box->setRange(std::numeric_limits<int>::min(), std::numeric_limits<int>::max());
        box->setKeyboardTracking(false);
        box->setValue(value.toInt());
        connect(box, &QSpinBox::editingFinished, this,
                [this, box, component, fieldName, before = value.toInt()]() mutable
                {
                    if (box->value() != before)
                    {
                        before = box->value(); // a second editingFinished (Enter, then focus leaving) records nothing
                        setField(component, fieldName, box->value());
                    }
                });
        return box;
    }
    if (type == u"float"_s)
    {
        QDoubleSpinBox* box = numberBox(value.toDouble(), parent);
        box->setObjectName(objectName);
        connect(box, &QDoubleSpinBox::editingFinished, this,
                [this, box, component, fieldName, before = box->value()]() mutable
                {
                    if (box->value() != before)
                    {
                        before = box->value();
                        setField(component, fieldName, box->value());
                    }
                });
        return box;
    }
    if (type == u"enum"_s)
    {
        auto* box = new QComboBox(parent);
        box->setObjectName(objectName);
        for (const QJsonValue& name : field.value(u"values").toArray())
        {
            box->addItem(name.toString());
        }
        box->setCurrentText(value.toString());
        connect(box, &QComboBox::activated, this,
                [this, box, component, fieldName](int) { setField(component, fieldName, box->currentText()); });
        return box;
    }
    if (type == u"vec3"_s || type == u"color"_s || type == u"quat"_s)
    {
        auto* row = new QWidget(parent);
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(0, 0, 0, 0);
        const QJsonArray numbers = value.toArray();
        std::vector<double> shown;
        QString hintKey;
        if (type == u"quat"_s)
        {
            // Euler angles in degrees; the ones the user typed while they still give the stored rotation.
            const Math::Quat rotation{
                static_cast<float>(numbers.at(0).toDouble()), static_cast<float>(numbers.at(1).toDouble()),
                static_cast<float>(numbers.at(2).toDouble()), static_cast<float>(numbers.at(3).toDouble(1.0))};
            hintKey = QString::fromStdString(m_shown->uuid().toString()) + u' ' + objectName;
            Math::Vec3 degrees{};
            const auto hint = m_typedAngles.find(hintKey);
            if (hint != m_typedAngles.end() &&
                sameRotation(Math::fromEuler({Math::toRadians(hint->second.x), Math::toRadians(hint->second.y),
                                              Math::toRadians(hint->second.z)}),
                             rotation))
            {
                degrees = hint->second;
            }
            else
            {
                const Math::Vec3 radians = Math::toEuler(rotation);
                degrees = {Math::toDegrees(radians.x), Math::toDegrees(radians.y), Math::toDegrees(radians.z)};
            }
            shown = {static_cast<double>(degrees.x), static_cast<double>(degrees.y), static_cast<double>(degrees.z)};
            row->setToolTip(u"Degrees: X pitch, Y yaw, Z roll"_s);
        }
        else
        {
            for (const QJsonValue& number : numbers)
            {
                shown.push_back(number.toDouble());
            }
        }
        std::vector<QDoubleSpinBox*> boxes;
        const QStringList letters =
            type == u"color"_s ? QStringList{u"R"_s, u"G"_s, u"B"_s, u"A"_s} : QStringList{u"X"_s, u"Y"_s, u"Z"_s};
        for (std::size_t i = 0; i < shown.size(); ++i)
        {
            if (i < static_cast<std::size_t>(letters.size()))
            {
                layout->addWidget(axisLabel(letters.at(static_cast<qsizetype>(i)), row));
            }
            QDoubleSpinBox* box = numberBox(shown[i], row);
            box->setObjectName(objectName + u'.' + QString::number(i));
            if (type == u"color"_s)
            {
                box->setRange(0.0, 1e6); // linear colour: 0 up, HDR above 1
            }
            layout->addWidget(box);
            boxes.push_back(box);
        }
        // Shared by every box of the row: what was last sent, so leaving a box without a change records nothing.
        auto last = std::make_shared<std::vector<double>>(shown);
        const auto commit = [this, boxes, component, fieldName, type, last, hintKey]
        {
            std::vector<double> now;
            for (const QDoubleSpinBox* box : boxes)
            {
                now.push_back(box->value());
            }
            if (now == *last)
            {
                return; // focus left without a change: nothing to record
            }
            *last = now;
            if (type == u"quat"_s)
            {
                const Math::Vec3 degrees{static_cast<float>(now[0]), static_cast<float>(now[1]),
                                         static_cast<float>(now[2])};
                m_typedAngles[hintKey] = degrees;
                const Math::Quat rotation = Math::fromEuler(
                    {Math::toRadians(degrees.x), Math::toRadians(degrees.y), Math::toRadians(degrees.z)});
                setField(component, fieldName,
                         QJsonArray{static_cast<double>(rotation.x), static_cast<double>(rotation.y),
                                    static_cast<double>(rotation.z), static_cast<double>(rotation.w)});
                return;
            }
            QJsonArray values;
            for (const double number : now)
            {
                values.append(number);
            }
            setField(component, fieldName, values);
        };
        for (QDoubleSpinBox* box : boxes)
        {
            connect(box, &QDoubleSpinBox::editingFinished, this, commit);
        }
        return row;
    }
    if (type == u"asset"_s || type == u"entity"_s)
    {
        // Names to pick from, the ID behind each (item data): people read names, files and commands keep IDs.
        auto* box = new QComboBox(parent);
        box->setObjectName(objectName);
        box->addItem(u"(none)"_s, QString{});
        std::vector<std::pair<QString, QString>> choices; // name, ID text
        if (type == u"asset"_s)
        {
            for (const auto& [mesh, name] : m_world.meshNames())
            {
                choices.emplace_back(QString::fromStdString(name), QString::fromStdString(mesh.uuid().toString()));
            }
        }
        else
        {
            const auto& registry = m_world.scene().registry();
            for (const auto [entity, idComponent] : registry.storage<Scene::IdComponent>()->each())
            {
                if (m_shown && idComponent.id == *m_shown)
                {
                    continue; // not itself (its own parent, its own target)
                }
                const auto* named = registry.try_get<Scene::NameComponent>(entity);
                const QString name =
                    named != nullptr && !named->name.empty() ? QString::fromStdString(named->name) : u"(unnamed)"_s;
                choices.emplace_back(name, QString::fromStdString(idComponent.id.uuid().toString()));
            }
            std::ranges::sort(choices);
        }
        // Several entities (or meshes) can share a name: those get the start of their ID, to tell them apart.
        for (std::size_t i = 0; i < choices.size(); ++i)
        {
            const bool repeated = (i > 0 && choices[i - 1].first == choices[i].first) ||
                                  (i + 1 < choices.size() && choices[i + 1].first == choices[i].first);
            box->addItem(repeated ? choices[i].first + u" ("_s + choices[i].second.left(8) + u')' : choices[i].first,
                         choices[i].second);
        }
        const QString current = value.toString();
        int index = box->findData(current);
        if (index < 0)
        {
            // Not loaded (a mesh whose model is missing) or gone: shown by its ID, so nothing is lost on screen.
            box->addItem(u"(not found) "_s + current, current);
            index = box->count() - 1;
        }
        box->setCurrentIndex(index);
        box->setToolTip(current.isEmpty() ? u"None"_s : u"ID: "_s + current);
        connect(box, &QComboBox::activated, this,
                [this, box, component, fieldName](int chosen)
                {
                    const QString picked = box->itemData(chosen).toString();
                    if (component == u"Parent"_s && m_shown)
                    {
                        // Re-parenting goes through its own command: it keeps the hierarchy right and refuses cycles.
                        run(u"entity.setParent"_s,
                            {{u"id"_s, QString::fromStdString(m_shown->uuid().toString())}, {u"parent"_s, picked}});
                        return;
                    }
                    setField(component, fieldName, picked);
                });
        return box;
    }
    // string: text.
    auto* edit = new QLineEdit(value.toString(), parent);
    edit->setObjectName(objectName);
    connect(edit, &QLineEdit::editingFinished, this,
            [this, edit, component, fieldName, before = value.toString()]() mutable
            {
                if (edit->text() != before)
                {
                    before = edit->text();
                    setField(component, fieldName, edit->text());
                }
            });
    return edit;
}
} // namespace Meow2::Editor
