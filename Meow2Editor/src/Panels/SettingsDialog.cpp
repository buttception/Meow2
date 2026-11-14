/// @file
/// @brief SettingsDialog (SettingsDialog.h).

#include "Panels/SettingsDialog.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QJsonObject>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace Meow2::Editor
{
using namespace Qt::StringLiterals;

SettingsDialog::SettingsDialog(EditorCommandRegistry& commands, const EditorSettings& settings, QWidget* parent)
    : QDialog(parent)
    , m_commands(commands)
    , m_settings(settings)
{
    setObjectName(u"SettingsDialog"_s);
    setWindowTitle(u"Settings"_s);
    auto* layout = new QVBoxLayout(this); // Qt parent–child ownership throughout
    auto* form = new QFormLayout();
    for (const SettingInfo& info : m_settings.list())
    {
        const QString key = info.key;
        QWidget* field = nullptr;
        if (info.type == SettingType::Bool)
        {
            auto* box = new QCheckBox(this);
            connect(box, &QCheckBox::toggled, this, [this, key](bool on) { apply(key, on); });
            field = box;
        }
        else
        {
            auto* spin = new QSpinBox(this);
            // QSpinBox holds an int; every editor setting's range fits.
            spin->setRange(static_cast<int>(info.minimum), static_cast<int>(info.maximum));
            spin->setKeyboardTracking(false); // one command when typing is done, not one per digit
            connect(spin, &QSpinBox::valueChanged, this,
                    [this, key](int value) { apply(key, static_cast<double>(value)); });
            field = spin;
        }
        field->setToolTip(info.description);
        field->setObjectName(key);
        m_fields.emplace(key, field);
        form->addRow(info.needsRestart ? info.label + u" *"_s : info.label, field);
    }
    layout->addLayout(form);
    auto* note = new QLabel(u"* takes effect after the editor restarts."_s, this);
    note->setEnabled(false); // dimmed, like a hint
    layout->addWidget(note);
    m_status = new QLabel(this);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
    layout->addWidget(buttons);
    refresh();
}

void SettingsDialog::refresh()
{
    for (const auto& [key, field] : m_fields)
    {
        // Blocked, so showing a value doesn't run settings.set for it.
        const QSignalBlocker block{field};
        if (auto* box = qobject_cast<QCheckBox*>(field))
        {
            box->setChecked(m_settings.value(key).toBool());
        }
        else if (auto* spin = qobject_cast<QSpinBox*>(field))
        {
            spin->setValue(static_cast<int>(m_settings.intValue(key)));
        }
    }
}

QWidget* SettingsDialog::field(const QString& key) const
{
    const auto found = m_fields.find(key);
    return found == m_fields.end() ? nullptr : found->second;
}

void SettingsDialog::apply(const QString& key, const QJsonValue& value)
{
    const Result<QJsonValue> applied =
        m_commands.execute(u"settings.set", QJsonObject{{u"key"_s, key}, {u"value"_s, value}});
    m_status->setText(applied ? QString{} : QString::fromStdString(applied.error().message));
    if (!applied)
    {
        refresh(); // show the value that's really stored
    }
}
} // namespace Meow2::Editor
