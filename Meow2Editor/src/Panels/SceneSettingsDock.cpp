/// @file
/// @brief SceneSettingsDock (SceneSettingsDock.h).

#include "Panels/SceneSettingsDock.h"

#include "Panels/FieldWidgets.h"

#include <memory>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QScrollArea>
#include <QVBoxLayout>
#include <vector>

namespace Meow2::Editor
{
using namespace Qt::StringLiterals;

namespace
{
/// @brief A field name for people: "sunElevation" → "Sun elevation".
QString labelFor(const QString& name)
{
    QString out;
    for (const QChar c : name)
    {
        if (c.isUpper() && !out.isEmpty())
        {
            out += u' ';
            out += c.toLower();
        }
        else
        {
            out += out.isEmpty() ? c.toUpper() : c;
        }
    }
    return out;
}
} // namespace

SceneSettingsDock::SceneSettingsDock(EditorCommandRegistry& commands, const EditorWorld& world, Log::Logger& log,
                                     QWidget* parent)
    : QDockWidget(u"Scene settings"_s, parent)
    , m_commands(commands)
    , m_world(world)
    , m_log(log)
{
    setObjectName(u"SceneSettingsDock"_s); // QMainWindow::saveState() keys docks by object name
    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setWidget(scroll);
}

void SceneSettingsDock::run(const QString& command, const QJsonObject& parameters)
{
    if (const Result<QJsonValue> ran = m_commands.execute(command, parameters); !ran)
    {
        MEOW2_LOG_WARN(m_log, "{}: {}", command.toStdString(), ran.error());
    }
}

void SceneSettingsDock::refresh()
{
    if (m_world.revision() == m_shownRevision)
    {
        return;
    }
    m_shownRevision = m_world.revision();
    build();
}

void SceneSettingsDock::build()
{
    auto* scroll = static_cast<QScrollArea*>(widget());
    auto* content = new QWidget();
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(6, 6, 6, 6);
    scroll->setWidget(content); // deletes the previous fields

    const Result<QJsonValue> list = m_commands.execute(u"component.list");
    const Result<QJsonValue> values = m_commands.execute(u"scene.settings");
    if (!list || !values)
    {
        layout->addWidget(new QLabel(u"The scene's settings can't be read."_s, content));
        return;
    }
    for (const QJsonValue& entry : list->toObject().value(u"settings").toArray())
    {
        const QJsonObject setting = entry.toObject();
        const QString name = setting.value(u"name").toString();
        const QJsonObject current = values->toObject().value(name).toObject();
        auto* title = new QLabel(name, content);
        title->setStyleSheet(u"font-weight: 600; padding: 2px 0;"_s);
        layout->addWidget(title);
        auto* form = new QFormLayout();
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        for (const QJsonValue& fieldValue : setting.value(u"fields").toArray())
        {
            const QJsonObject field = fieldValue.toObject();
            const QString fieldName = field.value(u"name").toString();
            const QString type = field.value(u"type").toString();
            const QJsonValue value = current.value(fieldName);
            const QString objectName = name + u'.' + fieldName;
            if (type == u"float"_s)
            {
                QDoubleSpinBox* box = numberBox(value.toDouble(), content);
                box->setObjectName(objectName);
                connect(box, &QDoubleSpinBox::editingFinished, this,
                        [this, box, name, fieldName, before = box->value()]() mutable
                        {
                            if (box->value() != before)
                            {
                                before = box->value(); // Enter then focus leaving records one step
                                run(u"scene.setSetting"_s,
                                    {{u"setting"_s, name}, {u"field"_s, fieldName}, {u"value"_s, box->value()}});
                            }
                        });
                form->addRow(labelFor(fieldName), box);
            }
            else if (type == u"color"_s)
            {
                auto* row = new QWidget(content);
                auto* rowLayout = new QHBoxLayout(row);
                rowLayout->setContentsMargins(0, 0, 0, 0);
                const QJsonArray numbers = value.toArray();
                std::vector<QDoubleSpinBox*> boxes;
                const QStringList letters{u"R"_s, u"G"_s, u"B"_s, u"A"_s};
                for (qsizetype i = 0; i < 4; ++i)
                {
                    rowLayout->addWidget(axisLabel(letters.at(i), row));
                    QDoubleSpinBox* box = numberBox(numbers.at(i).toDouble(1.0), row);
                    box->setObjectName(objectName + u'.' + QString::number(i));
                    box->setRange(0.0, 1e6); // linear colour: 0 up, HDR above 1
                    rowLayout->addWidget(box);
                    boxes.push_back(box);
                }
                auto last = std::make_shared<QJsonArray>(numbers);
                const auto commit = [this, boxes, name, fieldName, last]
                {
                    QJsonArray now;
                    for (const QDoubleSpinBox* box : boxes)
                    {
                        now.append(box->value());
                    }
                    if (now != *last)
                    {
                        *last = now;
                        run(u"scene.setSetting"_s, {{u"setting"_s, name}, {u"field"_s, fieldName}, {u"value"_s, now}});
                    }
                };
                for (QDoubleSpinBox* box : boxes)
                {
                    connect(box, &QDoubleSpinBox::editingFinished, this, commit);
                }
                form->addRow(labelFor(fieldName), row);
            }
            else
            {
                form->addRow(labelFor(fieldName), new QLabel(value.toVariant().toString(), content));
            }
        }
        layout->addLayout(form);
    }
    layout->addStretch(1);
}
} // namespace Meow2::Editor
