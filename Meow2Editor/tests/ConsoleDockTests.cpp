/// @file
/// @brief Tests for ConsoleDock, the editor's log console.

#include "EditorTheme.h"
#include "Panels/ConsoleDock.h"
#include "QtTestApplication.h"

#include <Meow2/Core/Log/ConsoleBuffer.h>

#include <QApplication>
#include <QClipboard>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextDocument>

namespace
{
using namespace Qt::StringLiterals;
using Meow2::Log::LogLevel;

/// @brief A dock over a buffer with three lines: info, warn, error.
class ConsoleDockTest : public Meow2::Tests::QtTest
{
protected:
    ConsoleDockTest()
    {
        m_buffer.add(LogLevel::Info, "10:00:00.000", "Engine", "Started");
        m_buffer.add(LogLevel::Warn, "10:00:01.000", "Editor", "Disk almost full");
        m_buffer.add(LogLevel::Error, "10:00:02.000", "Engine", "Shader failed");
        m_dock.tick();
    }

    /// @brief The colour of the character at `position` in the text area.
    QColor colorAt(int position)
    {
        QTextCursor cursor{m_dock.view().document()};
        cursor.setPosition(position + 1); // the format of the character before the cursor
        return cursor.charFormat().foreground().color();
    }

    Meow2::Log::ConsoleBuffer m_buffer{100};
    Meow2::Editor::ConsoleDock m_dock{m_buffer, true};
};
} // namespace

TEST_F(ConsoleDockTest, ColoursOnlyTheLevel)
{
    const QTextBlock warning = m_dock.view().document()->findBlockByNumber(1);
    ASSERT_EQ(warning.text(), u"[10:00:01.000]<warn> Editor: Disk almost full"_s);
    const int start = warning.position();
    const int tag = static_cast<int>(warning.text().indexOf(u"<warn>"));
    const int message = static_cast<int>(warning.text().indexOf(u"Disk"));
    EXPECT_EQ(colorAt(start + tag + 1), Meow2::Editor::consoleLevelColor(LogLevel::Warn)) << "the level is coloured";
    EXPECT_EQ(colorAt(start + message), QColor::fromRgb(Meow2::Editor::EditorColors::c_text)) << "the message isn't";
    EXPECT_EQ(colorAt(start), QColor::fromRgb(Meow2::Editor::EditorColors::c_text)) << "nor is the time";

    // Every level's colour stays readable on the console's background.
    for (LogLevel level : {LogLevel::Debug, LogLevel::Info, LogLevel::Warn, LogLevel::Error})
    {
        EXPECT_GE(Meow2::Editor::contrastRatio(Meow2::Editor::consoleLevelColor(level),
                                               QColor::fromRgb(Meow2::Editor::EditorColors::c_panel)),
                  4.5);
    }
}

TEST_F(ConsoleDockTest, FiltersByLevelAndText)
{
    EXPECT_EQ(m_dock.view().document()->blockCount(), 3);
    m_dock.setLevelShown(LogLevel::Info, false);
    EXPECT_EQ(m_dock.view().toPlainText(),
              u"[10:00:01.000]<warn> Editor: Disk almost full\n[10:00:02.000]<error> Engine: Shader failed"_s);

    m_dock.setFilterText(u"engine"_s); // matches the logger, ignoring case
    EXPECT_EQ(m_dock.view().toPlainText(), u"[10:00:02.000]<error> Engine: Shader failed"_s);

    m_dock.setLevelShown(LogLevel::Info, true);
    m_dock.setFilterText({});
    m_buffer.add(LogLevel::Debug, "10:00:03.000", "Engine", "Frame 1");
    m_dock.tick();
    EXPECT_EQ(m_dock.view().document()->blockCount(), 4) << "new lines arrive with the next tick";

    m_dock.clear();
    EXPECT_TRUE(m_dock.view().toPlainText().isEmpty());
    m_buffer.add(LogLevel::Info, "10:00:04.000", "Engine", "After clear");
    m_dock.tick();
    EXPECT_EQ(m_dock.view().toPlainText(), u"[10:00:04.000]<info> Engine: After clear"_s);
}

TEST_F(ConsoleDockTest, CopiesShownLines)
{
    m_dock.setLevelShown(LogLevel::Error, false);
    m_dock.copy();
    EXPECT_EQ(QApplication::clipboard()->text(),
              u"[10:00:00.000]<info> Engine: Started\n[10:00:01.000]<warn> Editor: Disk almost full"_s);
}
