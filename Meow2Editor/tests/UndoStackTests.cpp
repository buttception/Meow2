/// @file
/// @brief Tests for UndoStack, the editor's undo/redo history.

#include "Commands/UndoStack.h"

#include <gtest/gtest.h>

#include <QString>

namespace
{
using namespace Qt::StringLiterals;
using Meow2::Editor::UndoStack;
using Meow2::Editor::UndoStep;

/// @brief A step that moves `value` between `from` and `to`, labelled with `to`.
UndoStep step(int& value, int from, int to)
{
    value = to;
    return {.label = QString::number(to),
            .undo = [&value, from] { value = from; },
            .redo =
                [&value, to]
            {
                value = to;
            }};
}
} // namespace

TEST(UndoStackTest, NewStepClearsRedo)
{
    UndoStack history;
    int value = 0;
    history.push(step(value, 0, 1));
    history.push(step(value, 1, 2));
    ASSERT_TRUE(history.undo());
    EXPECT_EQ(value, 1);
    ASSERT_TRUE(history.canRedo());

    history.push(step(value, 1, 3));
    EXPECT_FALSE(history.canRedo()) << "a new step drops the undone one";
    EXPECT_EQ(history.undoLabels(), (QStringList{u"3"_s, u"1"_s}));
    EXPECT_FALSE(history.redo());
    ASSERT_TRUE(history.undo());
    ASSERT_TRUE(history.undo());
    EXPECT_EQ(value, 0);
    EXPECT_FALSE(history.undo()) << "nothing left";
}

TEST(UndoStackTest, LimitDropsOldest)
{
    UndoStack history{3};
    int value = 0;
    for (int i = 1; i <= 5; ++i)
    {
        history.push(step(value, i - 1, i));
    }
    EXPECT_EQ(history.undoLabels(), (QStringList{u"5"_s, u"4"_s, u"3"_s}));
    while (history.undo())
    {
    }
    EXPECT_EQ(value, 2) << "steps 1 and 2 were dropped, so undo stops after step 3";

    history.setLimit(1);
    EXPECT_EQ(history.redoLabels(), QStringList{u"3"_s}) << "with nothing done, the furthest redo steps go";
}

TEST(UndoStackTest, TracksCleanState)
{
    UndoStack history{2};
    int value = 0;
    EXPECT_TRUE(history.isClean()) << "a new history is clean";
    history.push(step(value, 0, 1));
    EXPECT_FALSE(history.isClean());
    history.undo();
    EXPECT_TRUE(history.isClean()) << "undo went back to the saved state";
    history.redo();
    history.markClean(); // saved after step 1
    EXPECT_TRUE(history.isClean());

    history.push(step(value, 1, 2));
    history.push(step(value, 2, 3)); // the limit drops step 1; the saved state (after it) is now the start
    EXPECT_FALSE(history.isClean());
    history.undo();
    EXPECT_FALSE(history.isClean());
    history.undo();
    EXPECT_TRUE(history.isClean()) << "the saved state is still reachable: it's where the kept steps start";

    history.redo();
    history.redo();
    history.push(step(value, 3, 4)); // the limit drops step 2, and with it the way back to the saved state
    while (history.undo())
    {
    }
    EXPECT_FALSE(history.isClean()) << "the saved state was before a dropped step";

    history.redo();
    history.markClean();
    history.undo();
    history.push(step(value, 2, 5)); // drops the redo step the saved state came after
    EXPECT_FALSE(history.isClean());
    history.undo();
    EXPECT_FALSE(history.isClean()) << "the saved state was on the dropped redo branch";
}

TEST(UndoStackTest, RevisionChangesWithEveryChange)
{
    UndoStack history;
    int value = 0;
    const auto before = history.revision();
    history.push(step(value, 0, 1));
    const auto afterPush = history.revision();
    EXPECT_NE(afterPush, before);
    history.undo();
    EXPECT_NE(history.revision(), afterPush);
}
