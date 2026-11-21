/// @file
/// @brief Tests for WheelGuard: the mouse wheel changes a value only in a focused field; otherwise the panel scrolls.

#include "Panels/WheelGuard.h"
#include "QtTestApplication.h"

#include <QApplication>
#include <QComboBox>
#include <QLineEdit>
#include <QScrollArea>
#include <QScrollBar>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <QWidget>

namespace
{
using namespace Qt::StringLiterals;
using WheelGuardTest = Meow2::Tests::QtTest;

/// @brief One notch of the wheel, away from the user (up), over a widget.
void wheelUp(QWidget& widget)
{
    const QPointF at{widget.width() / 2.0, widget.height() / 2.0};
    QWheelEvent event(at, widget.mapToGlobal(at), QPoint{}, QPoint{0, 120}, Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(&widget, &event);
}

/// @brief One notch down.
void wheelDown(QWidget& widget)
{
    const QPointF at{widget.width() / 2.0, widget.height() / 2.0};
    QWheelEvent event(at, widget.mapToGlobal(at), QPoint{}, QPoint{0, -120}, Qt::NoButton, Qt::NoModifier,
                      Qt::NoScrollPhase, false);
    QApplication::sendEvent(&widget, &event);
}
} // namespace

TEST_F(WheelGuardTest, WheelEditsOnlyFocusedFields)
{
    Meow2::Editor::WheelGuard guard;
    qApp->installEventFilter(&guard);

    // A tall panel in a scroll area, with a spin box and a combo box in it, as the Inspector has.
    QWidget window;
    auto* layout = new QVBoxLayout(&window);
    auto* scroll = new QScrollArea(&window);
    layout->addWidget(scroll);
    auto* content = new QWidget();
    auto* fields = new QVBoxLayout(content);
    auto* spin = new QSpinBox(content);
    spin->setRange(0, 100);
    spin->setValue(50);
    auto* combo = new QComboBox(content);
    combo->addItems({u"A"_s, u"B"_s, u"C"_s});
    combo->setCurrentIndex(1);
    auto* other = new QLineEdit(content);
    fields->addWidget(spin);
    fields->addWidget(combo);
    fields->addWidget(other);
    content->setMinimumHeight(2000);
    scroll->setWidget(content);
    window.resize(300, 200);
    window.show();
    window.activateWindow();
    QApplication::processEvents();

    EXPECT_EQ(spin->focusPolicy(), Qt::StrongFocus) << "the wheel doesn't focus it";
    EXPECT_EQ(combo->focusPolicy(), Qt::StrongFocus);

    // Not focused: the values stay, and the panel scrolls instead.
    other->setFocus();
    QApplication::processEvents();
    const int scrolledBefore = scroll->verticalScrollBar()->value();
    wheelDown(*spin);
    wheelDown(*combo);
    EXPECT_EQ(spin->value(), 50);
    EXPECT_EQ(combo->currentIndex(), 1);
    EXPECT_GT(scroll->verticalScrollBar()->value(), scrolledBefore) << "the wheel scrolled the panel";

    // Focused: the wheel edits it, as before.
    spin->setFocus();
    QApplication::processEvents();
    ASSERT_TRUE(spin->hasFocus());
    wheelUp(*spin);
    EXPECT_EQ(spin->value(), 51);
}
