/// @file
/// @brief The Meow2 Qt theme (EditorTheme.h).

#include "EditorTheme.h"

#include <algorithm>
#include <cmath>
#include <QFontDatabase>
#include <QStyleFactory>
#include <string>

namespace Meow2::Editor
{
QPalette editorPalette()
{
    using namespace EditorColors;
    QPalette palette;
    palette.setColor(QPalette::Window, QColor{c_base});
    palette.setColor(QPalette::WindowText, QColor{c_text});
    palette.setColor(QPalette::Base, QColor{c_panel});
    palette.setColor(QPalette::AlternateBase, QColor{0xFF111113});
    palette.setColor(QPalette::ToolTipBase, QColor{c_panel});
    palette.setColor(QPalette::ToolTipText, QColor{c_text});
    palette.setColor(QPalette::PlaceholderText, QColor{c_textDim});
    palette.setColor(QPalette::Text, QColor{c_text});
    palette.setColor(QPalette::Button, QColor{c_frame});
    palette.setColor(QPalette::ButtonText, QColor{c_text});
    palette.setColor(QPalette::BrightText, QColor{c_accentBright});
    palette.setColor(QPalette::Light, QColor{c_line});
    palette.setColor(QPalette::Midlight, QColor{c_frameHovered});
    palette.setColor(QPalette::Mid, QColor{c_line});
    palette.setColor(QPalette::Dark, QColor{c_base});
    palette.setColor(QPalette::Shadow, QColor{c_base});
    palette.setColor(QPalette::Highlight, QColor{c_accent});
    palette.setColor(QPalette::HighlightedText, QColor{0xFFFFFFFF});
    palette.setColor(QPalette::Link, QColor{c_accentBright});
    palette.setColor(QPalette::LinkVisited, QColor{c_accent});
    palette.setColor(QPalette::Accent, QColor{c_accent});
    for (const QPalette::ColorRole role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
    {
        palette.setColor(QPalette::Disabled, role, QColor{c_textDim});
    }
    palette.setColor(QPalette::Disabled, QPalette::Highlight, QColor{c_line});
    return palette;
}

QString editorStyleSheet()
{
    // The values are the palette's (EditorColors); a style sheet can't name constants, so they're written out.
    return QStringLiteral(R"(
QWidget { color: #E6E6E6; }
QMainWindow, QDialog { background: #000000; }
QMainWindow::separator { background: #34343A; width: 1px; height: 1px; }
QMainWindow::separator:hover { background: #E01E2D; }

QMenuBar { background: #000000; border-bottom: 1px solid #34343A; }
QMenuBar::item { padding: 4px 10px; background: transparent; }
QMenuBar::item:selected { background: rgba(224, 30, 45, 0.30); }
QMenu { background: #0C0C0D; border: 1px solid #34343A; padding: 4px 0; }
QMenu::item { padding: 5px 24px 5px 14px; }
QMenu::item:selected { background: rgba(224, 30, 45, 0.30); }
QMenu::separator { height: 1px; background: #34343A; margin: 4px 8px; }
QToolTip { background: #0C0C0D; color: #E6E6E6; border: 1px solid #E01E2D; padding: 4px; }

QPushButton, QToolButton { background: #18181B; border: 1px solid #34343A; padding: 4px 12px; }
QPushButton:hover, QToolButton:hover { border-color: #E01E2D; color: #FF5A66; background: rgba(224, 30, 45, 0.12); }
QPushButton:pressed, QToolButton:pressed, QToolButton:checked { background: rgba(224, 30, 45, 0.45); color: #FFFFFF; }
QPushButton:focus { border-color: #E01E2D; }
QPushButton:disabled, QToolButton:disabled { color: #8A8A90; border-color: #222226; }

QLineEdit, QSpinBox, QDoubleSpinBox, QComboBox, QPlainTextEdit, QTextEdit {
    background: #18181B; border: 1px solid #34343A; padding: 3px 5px;
    selection-background-color: #E01E2D; selection-color: #FFFFFF; }
QLineEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus, QTextEdit:focus {
    border-color: #E01E2D; }
QComboBox QAbstractItemView { background: #0C0C0D; border: 1px solid #34343A; selection-background-color: rgba(224, 30, 45, 0.30); }

QTreeView, QListView, QTableView { background: #0C0C0D; border: 1px solid #34343A; alternate-background-color: #111113; outline: 0;
    selection-background-color: rgba(224, 30, 45, 0.20); selection-color: #FFFFFF; }
QTreeView::item, QListView::item, QTableView::item { padding: 3px 4px; }
QTreeView::item:hover, QListView::item:hover, QTableView::item:hover { background: #18181B; }
QTreeView::item:selected, QListView::item:selected, QTableView::item:selected {
    background: rgba(224, 30, 45, 0.20); border-left: 2px solid #E01E2D; color: #FFFFFF; }
QTreeView::branch { background: transparent; }
QTreeView::branch:selected { background: rgba(224, 30, 45, 0.20); border-left: 2px solid #E01E2D; }
QTreeView::item:selected { border-left: none; }
QHeaderView::section { background: #000000; color: #8A8A90; border: none; border-bottom: 1px solid #34343A; padding: 4px 6px; }

QDockWidget::title { background: #000000; border-left: 3px solid #E01E2D; border-bottom: 1px solid #34343A; padding: 5px 8px; text-align: left; }

QTabWidget::pane { border: 1px solid #34343A; top: -1px; }
QTabBar::tab { background: #0C0C0D; color: #8A8A90; padding: 6px 14px; border: none; border-bottom: 2px solid transparent; }
QTabBar::tab:selected { background: #18181B; color: #E6E6E6; border-bottom: 2px solid #E01E2D; }
QTabBar::tab:hover:!selected { color: #FF5A66; }

QScrollBar:vertical { background: #000000; width: 10px; margin: 0; }
QScrollBar:horizontal { background: #000000; height: 10px; margin: 0; }
QScrollBar::handle { background: #34343A; min-height: 24px; min-width: 24px; }
QScrollBar::handle:hover { background: #E01E2D; }
QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }

QCheckBox::indicator, QRadioButton::indicator { width: 12px; height: 12px; background: #18181B; border: 1px solid #34343A; }
QCheckBox::indicator:checked, QRadioButton::indicator:checked { background: #E01E2D; border-color: #E01E2D; }
QCheckBox::indicator:hover, QRadioButton::indicator:hover { border-color: #E01E2D; }
QSlider::groove:horizontal { height: 2px; background: #34343A; }
QSlider::handle:horizontal { width: 8px; margin: -6px 0; background: #E01E2D; }
QProgressBar { background: #18181B; border: 1px solid #34343A; text-align: center; }
QProgressBar::chunk { background: #E01E2D; }
QSplitter::handle { background: #34343A; }
QSplitter::handle:hover { background: #E01E2D; }
QStatusBar { background: #000000; border-top: 1px solid #34343A; color: #8A8A90; }
)");
}

QFont editorMonospaceFont(int pointSize)
{
    QFont font{QStringLiteral("Consolas"), pointSize};
    // Where Consolas isn't installed (Linux), Qt falls back to the system's fixed-width font.
    font.setStyleHint(QFont::Monospace, QFont::PreferDefault);
    font.setFixedPitch(true);
    return font;
}

void applyEditorTheme(QApplication& application, const std::filesystem::path& fontFolder, Log::Logger& log)
{
    // Fusion draws every widget from the palette, the same way on every platform; the native Windows style ignores
    // much of it.
    QApplication::setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
    QApplication::setPalette(editorPalette());

    bool loaded = false;
    for (const char* file : {"BarlowSemiCondensed-Regular.ttf", "BarlowSemiCondensed-Medium.ttf"})
    {
        const std::filesystem::path path = fontFolder / "BarlowSemiCondensed" / file;
        // Qt takes the path as a QString; from the native string so non-ASCII folders work on Windows.
        const int id = QFontDatabase::addApplicationFont(QString::fromStdWString(path.wstring()));
        if (id < 0)
        {
            MEOW2_LOG_WARN(log, "UI font not loaded: {}", path.string());
        }
        loaded = loaded || id >= 0;
    }
    if (loaded)
    {
        QFont font{QString::fromLatin1(c_uiFontFamily), 10};
        font.setStyleStrategy(QFont::PreferAntialias);
        QApplication::setFont(font);
    }
    application.setStyleSheet(editorStyleSheet());
}

double contrastRatio(const QColor& a, const QColor& b)
{
    // WCAG 2 relative luminance: sRGB channels decoded to linear light, weighted by how bright each looks.
    const auto luminance = [](const QColor& color)
    {
        const auto linear = [](double channel)
        {
            return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
        };
        return 0.2126 * linear(color.redF()) + 0.7152 * linear(color.greenF()) + 0.0722 * linear(color.blueF());
    };
    const double first = luminance(a);
    const double second = luminance(b);
    return (std::max(first, second) + 0.05) / (std::min(first, second) + 0.05);
}
} // namespace Meow2::Editor
