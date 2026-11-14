/// @file
/// @brief ConsoleDock (ConsoleDock.h).

#include "Panels/ConsoleDock.h"

#include "EditorTheme.h"

#include <format>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QStringList>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QVBoxLayout>
#include <string_view>
#include <utility>
#include <vector>

namespace Meow2::Editor
{
using namespace Qt::StringLiterals;

namespace
{
/// @brief Level names as the log file writes them, in Log::LogLevel's order.
constexpr std::array<std::string_view, 5> c_levelNames{"debug", "info", "warn", "error", "fatal"};

/// @brief Yellow for warnings, as in PowerShell; the theme has no yellow of its own.
constexpr QRgb c_warnColor = 0xFFE5C07B;
} // namespace

QColor consoleLevelColor(Log::LogLevel level)
{
    switch (level)
    {
        case Log::LogLevel::Debug:
            return QColor::fromRgb(EditorColors::c_textDim);
        case Log::LogLevel::Info:
            return QColor::fromRgb(EditorColors::c_text);
        case Log::LogLevel::Warn:
            return QColor::fromRgb(c_warnColor);
        case Log::LogLevel::Error:
        case Log::LogLevel::Fatal:
            break;
    }
    return QColor::fromRgb(EditorColors::c_accentBright);
}

QString formatConsoleLine(const Log::LogLine& line)
{
    return QString::fromStdString(std::format(
        "[{}]<{}> {}: {}", line.time, c_levelNames[static_cast<std::size_t>(line.level)], line.logger, line.message));
}

ConsoleDock::ConsoleDock(const Log::ConsoleBuffer& buffer, bool followNewest, QWidget* parent)
    : QDockWidget(u"Console"_s, parent)
    , m_buffer(buffer)
{
    setObjectName(u"ConsoleDock"_s); // QMainWindow::saveState() keys docks by object name

    auto* content = new QWidget(this); // Qt parent–child ownership throughout
    auto* layout = new QVBoxLayout(content);
    layout->setContentsMargins(4, 4, 4, 4);
    auto* toolbar = new QHBoxLayout();
    for (std::size_t i = 0; i < m_levelBoxes.size(); ++i)
    {
        auto* box = new QCheckBox(QString::fromLatin1(c_levelNames[i].data()).toUpper(), content);
        box->setChecked(true);
        const auto level = static_cast<Log::LogLevel>(i);
        connect(box, &QCheckBox::toggled, this, [this, level](bool shown) { setLevelShown(level, shown); });
        toolbar->addWidget(box);
        m_levelBoxes[i] = box;
    }
    m_filterEdit = new QLineEdit(content);
    m_filterEdit->setPlaceholderText(u"Filter"_s);
    m_filterEdit->setClearButtonEnabled(true);
    connect(m_filterEdit, &QLineEdit::textChanged, this, [this](const QString& text) { setFilterText(text); });
    toolbar->addWidget(m_filterEdit, 1);
    auto* clearButton = new QPushButton(u"Clear"_s, content);
    connect(clearButton, &QPushButton::clicked, this, [this] { clear(); });
    toolbar->addWidget(clearButton);
    auto* copyButton = new QPushButton(u"Copy"_s, content);
    connect(copyButton, &QPushButton::clicked, this, [this] { copy(); });
    toolbar->addWidget(copyButton);
    m_follow = new QCheckBox(u"Follow"_s, content);
    m_follow->setChecked(followNewest);
    toolbar->addWidget(m_follow);
    layout->addLayout(toolbar);

    m_view = new QPlainTextEdit(content);
    m_view->setReadOnly(true);
    m_view->setLineWrapMode(QPlainTextEdit::NoWrap);
    m_view->setFont(editorMonospaceFont());
    m_view->setMaximumBlockCount(static_cast<int>(m_buffer.capacity())); // drop the oldest blocks as Qt goes
    layout->addWidget(m_view, 1);
    setWidget(content);
}

void ConsoleDock::tick()
{
    std::vector<Log::LogLine> fresh;
    m_buffer.linesSince(m_next, fresh);
    if (fresh.empty())
    {
        return;
    }
    m_next = fresh.back().sequence + 1;
    for (Log::LogLine& line : fresh)
    {
        if (passes(line))
        {
            appendLine(line);
        }
        m_lines.push_back(std::move(line));
    }
    while (m_lines.size() > m_buffer.capacity())
    {
        m_lines.pop_front();
    }
    if (m_follow->isChecked())
    {
        m_view->verticalScrollBar()->setValue(m_view->verticalScrollBar()->maximum());
    }
}

void ConsoleDock::setLevelShown(Log::LogLevel level, bool shown)
{
    const auto index = static_cast<std::size_t>(level);
    if (m_levelShown[index] == shown)
    {
        return;
    }
    m_levelShown[index] = shown;
    if (index < m_levelBoxes.size() && m_levelBoxes[index]->isChecked() != shown)
    {
        m_levelBoxes[index]->setChecked(shown); // keep the box in step when called from code
    }
    rebuild();
}

void ConsoleDock::setFilterText(const QString& text)
{
    if (m_filterText == text)
    {
        return;
    }
    m_filterText = text;
    if (m_filterEdit->text() != text)
    {
        m_filterEdit->setText(text);
    }
    rebuild();
}

void ConsoleDock::clear()
{
    m_lines.clear();
    m_view->clear();
}

void ConsoleDock::copy()
{
    const QString selected = m_view->textCursor().selectedText();
    // A selection spanning lines uses U+2029 as the separator; the clipboard wants newlines.
    QApplication::clipboard()->setText(
        selected.isEmpty() ? m_view->toPlainText() : QString{selected}.replace(QChar::ParagraphSeparator, u'\n'));
}

bool ConsoleDock::passes(const Log::LogLine& line) const
{
    if (!m_levelShown[static_cast<std::size_t>(line.level)])
    {
        return false;
    }
    return m_filterText.isEmpty() || QString::fromStdString(line.message).contains(m_filterText, Qt::CaseInsensitive) ||
           QString::fromStdString(line.logger).contains(m_filterText, Qt::CaseInsensitive);
}

void ConsoleDock::appendLine(const Log::LogLine& line)
{
    QTextCursor cursor{m_view->document()};
    cursor.movePosition(QTextCursor::End);
    if (!m_view->document()->isEmpty())
    {
        cursor.insertBlock();
    }
    QTextCharFormat normal;
    normal.setForeground(QColor::fromRgb(EditorColors::c_text));
    QTextCharFormat levelFormat = normal;
    levelFormat.setForeground(consoleLevelColor(line.level));

    cursor.insertText(QString::fromStdString(std::format("[{}]", line.time)), normal);
    cursor.insertText(QString::fromStdString(std::format("<{}>", c_levelNames[static_cast<std::size_t>(line.level)])),
                      levelFormat);
    cursor.insertText(QString::fromStdString(std::format(" {}: {}", line.logger, line.message)), normal);
}

void ConsoleDock::rebuild()
{
    m_view->clear();
    for (const Log::LogLine& line : m_lines)
    {
        if (passes(line))
        {
            appendLine(line);
        }
    }
}
} // namespace Meow2::Editor
