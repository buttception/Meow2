#pragma once

/// @file
/// @brief Small field widgets shared by the Inspector and the Scene settings dock: compact number boxes and the
///        coloured axis letters in front of vector rows.

#include <QDoubleSpinBox>
#include <QEvent>
#include <QLabel>
#include <QLineEdit>
#include <QResizeEvent>
#include <QSpinBox>
#include <QString>

namespace Meow2::Editor
{
/// @brief A number box that asks for little width: Qt sizes spin boxes for the widest number their range allows
///        (−1000000000.0000), which made every vector row wider than the panel. Without the arrow buttons too; drag or
///        the wheel (when focused) and typing change it.
template <class Box>
class CompactBox final : public Box
{
public:
    using Box::Box;

    /// @brief Room for "-0000.000".
    [[nodiscard]] QSize sizeHint() const override
    {
        return sizeFor(QStringLiteral("-0000.000"));
    }

    /// @brief Room for "-000.000" at least, so a value always shows whole when the panel is narrow.
    [[nodiscard]] QSize minimumSizeHint() const override
    {
        return sizeFor(QStringLiteral("-000.000"));
    }

protected:
    /// @brief Lays the text out again after the base class did (see placeEdit()).
    /// @param event The resize.
    void resizeEvent(QResizeEvent* event) override
    {
        Box::resizeEvent(event);
        placeEdit();
    }

    /// @brief Lays the text out again after a style change, where the base class places it too.
    /// @param event The change.
    void changeEvent(QEvent* event) override
    {
        Box::changeEvent(event);
        if (event->type() == QEvent::StyleChange || event->type() == QEvent::FontChange)
        {
            placeEdit();
        }
    }

private:
    static constexpr int c_insetX = 5; ///< The theme's 1 px border and 4 px padding (EditorTheme.cpp, compact boxes).
    static constexpr int c_insetY = 1; ///< The border; the text is centred in the rest.

    /// @brief Gives the text box inside all of the box but its frame and padding. With a style sheet, Qt places the
    ///        text box of a spin box without arrow buttons 1 px wide (QStyleSheetStyle's SC_SpinBoxEditField), so
    ///        no value showed whatever the box's width.
    void placeEdit()
    {
        if (QLineEdit* edit = this->lineEdit(); edit != nullptr)
        {
            edit->setGeometry(this->rect().adjusted(c_insetX, c_insetY, -c_insetX, -c_insetY));
        }
    }

    /// @brief The box's size for a sample text: the text, the line edit's own small margins and the insets above.
    ///        Not through the style: with a style sheet it still counts the hidden arrow buttons (116 px for "0.000").
    [[nodiscard]] QSize sizeFor(const QString& sample) const
    {
        constexpr int c_lineEditMargins = 6; // QLineEdit's 2 px text margin and 1 px frame on each side
        return {this->fontMetrics().horizontalAdvance(sample) + c_lineEditMargins + 2 * c_insetX,
                Box::sizeHint().height()};
    }
};

/// @brief A compact number box for a float field: three decimals, no arrow buttons, edits land when finished.
/// @param value  The value shown.
/// @param parent Its parent (which owns it).
/// @return The box.
inline QDoubleSpinBox* numberBox(double value, QWidget* parent)
{
    auto* box = new CompactBox<QDoubleSpinBox>(parent); // Qt parent–child ownership
    box->setRange(-1e9, 1e9);
    box->setDecimals(3);
    box->setSingleStep(0.1);
    box->setButtonSymbols(QAbstractSpinBox::NoButtons);
    box->setProperty("compact", true); // the theme then gives the hidden arrows no room (EditorTheme.cpp)
    box->setKeyboardTracking(false);   // edits land on Enter or when focus leaves, not on every key
    box->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    box->setValue(value);
    return box;
}

/// @brief A small coloured axis letter before a vector's number (X red, Y green, Z blue, as the gizmo).
/// @param text   X, Y, Z (red, green, blue) or another letter (grey).
/// @param parent Its parent (which owns it).
/// @return The label.
inline QLabel* axisLabel(const QString& text, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    const QString color = text == QStringLiteral("X")   ? QStringLiteral("#E5484D")
                          : text == QStringLiteral("Y") ? QStringLiteral("#46A758")
                          : text == QStringLiteral("Z") ? QStringLiteral("#3E7BFA")
                                                        : QStringLiteral("#8B8B93");
    label->setStyleSheet(QStringLiteral("color: %1; font-weight: 600;").arg(color));
    return label;
}
} // namespace Meow2::Editor
