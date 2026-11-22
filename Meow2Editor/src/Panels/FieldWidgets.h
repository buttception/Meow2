#pragma once

/// @file
/// @brief Small field widgets shared by the Inspector and the Scene settings dock: compact number boxes and the
///        coloured axis letters in front of vector rows.

#include <QDoubleSpinBox>
#include <QLabel>
#include <QSpinBox>
#include <QString>
#include <QStyle>
#include <QStyleOptionSpinBox>

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

private:
    /// @brief The box's size for a sample text, through the style: the theme's padding and the frame come on top of
    ///        the text (sizing from the text alone left no room, and the values didn't show).
    [[nodiscard]] QSize sizeFor(const QString& sample) const
    {
        QStyleOptionSpinBox option;
        this->initStyleOption(&option);
        const QSize text(this->fontMetrics().horizontalAdvance(sample) + 4, this->fontMetrics().height());
        return this->style()->sizeFromContents(QStyle::CT_SpinBox, &option, text, this).expandedTo(QSize(0, 0));
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
    box->setKeyboardTracking(false); // edits land on Enter or when focus leaves, not on every key
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
