/// @file
/// @brief The editor's theme: palette, contrast, and what applying it changes.

#include "QtTestApplication.h"

#include <EditorTheme.h>
#include <gtest/gtest.h>

#include <filesystem>
#include <QApplication>
#include <QDockWidget>
#include <QFontInfo>
#include <QImage>
#include <QLineEdit>
#include <QMainWindow>
#include <QMenuBar>
#include <QPushButton>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

namespace Meow2::Editor
{
namespace
{
using EditorThemeTest = Tests::QtTest;
} // namespace

TEST(EditorTheme, PaletteIsDarkWithRedHighlight)
{
    const QPalette palette = editorPalette();
    EXPECT_EQ(palette.color(QPalette::Window), QColor{EditorColors::c_base});
    EXPECT_EQ(palette.color(QPalette::Highlight), QColor{EditorColors::c_accent});
    EXPECT_EQ(palette.color(QPalette::Text), QColor{EditorColors::c_text});
    // WCAG AAA asks 7:1 for body text; light grey on black gives about 17:1.
    EXPECT_GE(contrastRatio(palette.color(QPalette::Text), palette.color(QPalette::Base)), 7.0);
    EXPECT_GE(contrastRatio(palette.color(QPalette::WindowText), palette.color(QPalette::Window)), 7.0);
    // Dimmed labels are still readable (AA, 4.5:1).
    EXPECT_GE(contrastRatio(QColor{EditorColors::c_textDim}, QColor{EditorColors::c_panel}), 4.5);
}

TEST(EditorTheme, ContrastRatioMatchesWcag)
{
    EXPECT_NEAR(contrastRatio(Qt::black, Qt::white), 21.0, 1e-9);
    EXPECT_NEAR(contrastRatio(Qt::white, Qt::white), 1.0, 1e-9);
}

TEST_F(EditorThemeTest, AppliesToTheApplication)
{
    applyEditorTheme(*qApp, MEOW2_FONTS_DIR, m_log);

    // The style itself isn't checked: with a style sheet set, Qt wraps it in its own QStyleSheetStyle.
    EXPECT_EQ(QApplication::palette().color(QPalette::Highlight), QColor{EditorColors::c_accent});
    EXPECT_FALSE(qApp->styleSheet().isEmpty());
    EXPECT_EQ(QFontInfo{QApplication::font()}.family(), QString::fromLatin1(c_uiFontFamily)) << "the bundled font";
    EXPECT_EQ(logText().find("UI font not loaded"), std::string::npos) << logText();
}

TEST_F(EditorThemeTest, MissingFontIsLoggedAndTheRestApplies)
{
    applyEditorTheme(*qApp, "no-such-folder", m_log);

    // The style and palette still apply; only the font is the system's.
    EXPECT_EQ(QApplication::palette().color(QPalette::Window), QColor{EditorColors::c_base});
    EXPECT_NE(logText().find("UI font not loaded"), std::string::npos);
}
TEST_F(EditorThemeTest, ThemedWidgetsDrawBlackAndRed)
{
    applyEditorTheme(*qApp, MEOW2_FONTS_DIR, m_log);
    // A stand-in for the editor's future layout: menu bar, a hierarchy dock with a selected row, tabs, a field.
    QMainWindow window;
    window.resize(800, 450);
    window.menuBar()->addMenu(QStringLiteral("File"));
    window.menuBar()->addMenu(QStringLiteral("Edit"));
    auto* dock = new QDockWidget(QStringLiteral("HIERARCHY"), &window);
    auto* tree = new QTreeWidget(dock);
    tree->setHeaderHidden(true);
    for (const char* name : {"Scene", "Camera", "Sun", "Helmet"})
    {
        tree->addTopLevelItem(new QTreeWidgetItem(QStringList{QString::fromLatin1(name)}));
    }
    tree->setCurrentItem(tree->topLevelItem(3));
    dock->setWidget(tree);
    window.addDockWidget(Qt::LeftDockWidgetArea, dock);
    auto* tabs = new QTabWidget(&window);
    auto* page = new QWidget(tabs);
    auto* layout = new QVBoxLayout(page);
    layout->addWidget(new QLineEdit(QStringLiteral("DamagedHelmet.gltf"), page));
    layout->addWidget(new QPushButton(QStringLiteral("CAPTURE"), page));
    layout->addStretch();
    tabs->addTab(page, QStringLiteral("Viewport"));
    tabs->addTab(new QWidget(tabs), QStringLiteral("Game"));
    window.setCentralWidget(tabs);
    window.show();
    QApplication::processEvents();

    const QImage image = window.grab().toImage();
    // Kept for review (the PR shows it); a failure to save doesn't fail the test.
    image.save(QString::fromStdWString((std::filesystem::temp_directory_path() / "meow2-editor-theme.png").wstring()));
    const QColor corner = image.pixelColor(image.width() - 4, image.height() - 4);
    EXPECT_LT(corner.lightness(), 40) << "the background is near black";
    const QRect selected = tree->visualItemRect(tree->topLevelItem(3));
    const QPoint inRow = tree->viewport()->mapTo(&window, selected.center() + QPoint{selected.width() / 3, 0});
    const QColor row = image.pixelColor(inRow);
    EXPECT_GT(row.red(), row.green() + 20) << "the selected row is tinted red";
}
} // namespace Meow2::Editor
