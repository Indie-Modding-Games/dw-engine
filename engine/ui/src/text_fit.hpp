#pragma once

#include <dwe/ui/ui.hpp>

#include <string_view>
#include <vector>

namespace Rml {
class Element;
class ElementDocument;
} // namespace Rml

namespace dwe::ui {

// The texts of a laid out document that do not fit (Ui::overflowingTexts): each text the player
// sees is checked against the box drawn round it, every element round it that clips its content
// and the window (`windowWidth` x `windowHeight` pixels), then against the edges of the boxes and
// the texts drawn beside it. What a later opaque box covers is hidden, not out of place. A text
// does not fit when it passes or overlaps one of them by more than one of the original's pixels
// (3dp); `dpRatio` gives the pixels per dp.
[[nodiscard]] std::vector<Ui::TextOverflow> findTextOverflows(Rml::ElementDocument& document,
                                                              std::string_view name, float dpRatio,
                                                              float windowWidth,
                                                              float windowHeight);

// What the player sees of a laid out document, or of one element of it, within the window
// (Ui::drawnRects). `leftOut`, when given, and all it holds are left out, and so are the boxes
// round it (an ancestor whose box holds it whole: its panel).
[[nodiscard]] std::vector<Ui::Rect> findDrawnRects(Rml::Element& root, float windowWidth,
                                                   float windowHeight,
                                                   Rml::Element* leftOut = nullptr);

} // namespace dwe::ui
