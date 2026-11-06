#include "text_fit.hpp"

#include <RmlUi/Core/Box.h>
#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/FontMetrics.h>

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace dwe::ui {
namespace {

struct Rect {
  float left = 0.0F;
  float top = 0.0F;
  float right = 0.0F;
  float bottom = 0.0F;
};

// How far `inner` passes `outer` on its worst side, in pixels (0 or less when it fits).
float overflowOf(const Rect& inner, const Rect& outer) {
  return std::max({outer.left - inner.left, outer.top - inner.top, inner.right - outer.right,
                   inner.bottom - outer.bottom});
}

bool drawsBorder(const Rml::ComputedValues& values) {
  return (values.border_top_width() > 0.0F && values.border_top_color().alpha > 0) ||
         (values.border_right_width() > 0.0F && values.border_right_color().alpha > 0) ||
         (values.border_bottom_width() > 0.0F && values.border_bottom_color().alpha > 0) ||
         (values.border_left_width() > 0.0F && values.border_left_color().alpha > 0);
}

bool drawsItself(const Rml::Element& element) {
  const Rml::ComputedValues& values = element.GetComputedValues();
  return values.has_decorator() || values.background_color().alpha > 0 || drawsBorder(values);
}

// Whether an element's frame is drawn by its empty children: two or more sprites side by side that
// cover it (a tab's left end, middle and right end).
bool framedByParts(Rml::Element& element) {
  int parts = 0;
  float covered = 0.0F;
  for (int i = 0; i < element.GetNumChildren(); ++i) {
    Rml::Element& child = *element.GetChild(i);
    if (child.GetNumChildren() == 0 && child.IsVisible() && drawsItself(child)) {
      ++parts;
      const Rml::Vector2f size = child.GetBox().GetSize(Rml::BoxArea::Border);
      covered += size.x * size.y;
    }
  }
  const Rml::Vector2f size = element.GetBox().GetSize(Rml::BoxArea::Border);
  return parts >= 2 && covered >= 0.9F * size.x * size.y;
}

// A box the player sees round a text: an element that draws a background, a border or a decorator
// (the original look's frames, its sprites), or one framed by parts.
bool drawsBox(Rml::Element& element) {
  return drawsItself(element) || framedByParts(element);
}

// A box that hides what is drawn under it.
bool opaque(const Rml::Element& element) {
  return element.GetComputedValues().background_color().alpha >= 0xF0;
}

// Characters that reach below the baseline; a line without them ends there.
bool reachesBelow(std::string_view line) {
  return line.find_first_of("gjpqy,;()[]{}|_@Q") != std::string_view::npos;
}

bool clips(const Rml::Element& element) {
  const Rml::ComputedValues& values = element.GetComputedValues();
  return values.overflow_x() != Rml::Style::Overflow::Visible ||
         values.overflow_y() != Rml::Style::Overflow::Visible;
}

// An element whose content scrolls up and down (a list longer than its box).
bool scrollsVertically(const Rml::Element& element) {
  const Rml::Style::Overflow overflow = element.GetComputedValues().overflow_y();
  return overflow == Rml::Style::Overflow::Auto || overflow == Rml::Style::Overflow::Scroll;
}

// The area inside an element's border, where its content and padding go.
Rect paddingRect(Rml::Element& element) {
  const Rml::Vector2f offset = element.GetAbsoluteOffset(Rml::BoxArea::Padding);
  const Rml::Vector2f size = element.GetBox().GetSize(Rml::BoxArea::Padding);
  return {
      .left = offset.x, .top = offset.y, .right = offset.x + size.x, .bottom = offset.y + size.y};
}

// A box's inside: within its CSS border when it draws one, else its whole border box (a decorator
// draws its frame there, round the text).
Rect insideOf(Rml::Element& element) {
  if (drawsBorder(element.GetComputedValues())) {
    return paddingRect(element);
  }
  const Rml::Vector2f offset = element.GetAbsoluteOffset(Rml::BoxArea::Border);
  const Rml::Vector2f size = element.GetBox().GetSize(Rml::BoxArea::Border);
  return {
      .left = offset.x, .top = offset.y, .right = offset.x + size.x, .bottom = offset.y + size.y};
}

// `tag#id.class` for one element, as the logs name it.
std::string nameOf(const Rml::Element& element) {
  std::string name = element.GetTagName();
  if (!element.GetId().empty()) {
    name += "#" + element.GetId();
  }
  std::string classes = element.GetClassNames();
  if (!classes.empty()) {
    std::ranges::replace(classes, ' ', '.');
    name += "." + classes;
  }
  return name;
}

// An element and its parent up to the nearest one with an id: enough to find it in the RML.
std::string pathOf(const Rml::Element& element) {
  std::string path = nameOf(element);
  const Rml::Element* parent = element.GetParentNode();
  if (element.GetId().empty() && parent != nullptr && parent->GetParentNode() != nullptr) {
    path = pathOf(*parent) + " > " + path;
  }
  return path;
}

// Whether the player sees the text: it and every element round it are shown, and it is not
// transparent. Texts under a transform are left out: the layout's offsets leave transforms out.
bool seen(Rml::ElementText& text) {
  const Rml::ComputedValues& values = text.GetComputedValues();
  if (values.opacity() <= 0.0F || values.color().alpha == 0) {
    return false;
  }
  for (Rml::Element* element = &text; element != nullptr; element = element->GetParentNode()) {
    if (!element->IsVisible() || element->GetComputedValues().has_local_transform()) {
      return false;
    }
  }
  return true;
}

// A text the player sees: its lines' extent and its element.
struct SeenText {
  Rml::Element* owner = nullptr;
  Rect extent;
  std::string shown;
  // Where it is drawn among the document's texts and boxes, in document order.
  std::size_t order = 0;
};

// The extent of a text's lines, and the lines joined with " / "; nothing for a blank text.
std::optional<SeenText> textOf(Rml::ElementText& text, float windowWidth, float windowHeight) {
  if (!seen(text) || text.GetLines().empty()) {
    return std::nullopt;
  }
  const Rml::FontMetrics& metrics =
      Rml::GetFontEngineInterface()->GetFontMetrics(text.GetFontFaceHandle());
  const Rml::Vector2f origin = text.GetAbsoluteOffset(Rml::BoxArea::Border);
  const float lineHeight = text.GetLineHeight();
  SeenText result{
      .owner = text.GetParentNode(),
      .extent = {.left = windowWidth, .top = windowHeight, .right = 0.0F, .bottom = 0.0F},
      .shown = {}};
  for (const Rml::ElementText::Line& line : text.GetLines()) {
    if (line.text.find_first_not_of(" \t\n") == std::string::npos) {
      continue;
    }
    // A line's position is where its baseline starts. Its height is the font's, within the line
    // box: a line height smaller than the font's ascent and descent cuts them (the modern font
    // leaves room above for accents), a larger one adds nothing the player sees.
    const float baseline = origin.y + line.position.y;
    const float leading = (lineHeight - (metrics.ascent + metrics.descent)) / 2.0F;
    const float lineTop = baseline - metrics.ascent - leading;
    Rect& extent = result.extent;
    extent.left = std::min(extent.left, origin.x + line.position.x);
    extent.right =
        std::max(extent.right, origin.x + line.position.x + static_cast<float>(line.width));
    extent.top = std::min(extent.top, std::max(baseline - metrics.ascent, lineTop));
    const float descent = reachesBelow(line.text) ? metrics.descent : 0.0F;
    extent.bottom = std::max(extent.bottom, std::min(baseline + descent, lineTop + lineHeight));
    result.shown += (result.shown.empty() ? "" : " / ") + line.text;
  }
  if (result.shown.empty() || result.owner == nullptr) {
    return std::nullopt;
  }
  return result;
}

// How far two rectangles overlap: the smaller side of their intersection (0 or less if apart).
float overlapOf(const Rect& a, const Rect& b) {
  return std::min(std::min(a.right, b.right) - std::max(a.left, b.left),
                  std::min(a.bottom, b.bottom) - std::max(a.top, b.top));
}

Rect borderRect(Rml::Element& element) {
  const Rml::Vector2f offset = element.GetAbsoluteOffset(Rml::BoxArea::Border);
  const Rml::Vector2f size = element.GetBox().GetSize(Rml::BoxArea::Border);
  return {
      .left = offset.x, .top = offset.y, .right = offset.x + size.x, .bottom = offset.y + size.y};
}

bool isAncestor(const Rml::Element& ancestor, const Rml::Element* element) {
  for (; element != nullptr; element = element->GetParentNode()) {
    if (element == &ancestor) {
      return true;
    }
  }
  return false;
}

// The worst of what a text passes among the elements round it: the box drawn round it, what clips
// it, the window.
std::pair<float, std::string> worstContainer(const SeenText& text, Rml::ElementDocument& document,
                                             const Rect& window) {
  float worst = overflowOf(text.extent, window);
  std::string against = "off the window";
  bool boxFound = false;
  for (Rml::Element* element = text.owner; element != nullptr && element != &document;
       element = element->GetParentNode()) {
    const bool box = !boxFound && drawsBox(*element);
    boxFound = boxFound || box;
    if (!box && !clips(*element)) {
      continue;
    }
    const float past =
        overflowOf(text.extent, clips(*element) ? paddingRect(*element) : insideOf(*element));
    if (past > worst) {
      worst = past;
      against = "out of " + pathOf(*element);
    }
  }
  return {worst, against};
}

} // namespace

std::vector<Ui::TextOverflow> findTextOverflows(Rml::ElementDocument& document,
                                                std::string_view name, float dpRatio,
                                                float windowWidth, float windowHeight) {
  std::vector<Ui::TextOverflow> found;
  if (!document.IsVisible()) {
    return found;
  }
  // One of the original's pixels: a font's cell has blank rows and columns round its glyphs.
  const float tolerance = 3.0F * dpRatio;
  const Rect window{.left = 0.0F, .top = 0.0F, .right = windowWidth, .bottom = windowHeight};

  // The texts the player sees and the boxes drawn, in document order: what comes later is drawn
  // over what came before (z-index left aside).
  std::vector<SeenText> texts;
  std::vector<Rml::Element*> boxes;
  std::unordered_map<const Rml::Element*, std::size_t> boxOrder;
  std::size_t order = 0;
  const auto walk = [&](this const auto& self, Rml::Element& element) -> void {
    if (!element.IsVisible() || element.GetComputedValues().has_local_transform()) {
      return;
    }
    if (auto* text = rmlui_dynamic_cast<Rml::ElementText*>(&element)) {
      if (auto seenText = textOf(*text, windowWidth, windowHeight)) {
        seenText->order = order++;
        texts.push_back(std::move(*seenText));
      }
    } else if (drawsItself(element) && element.GetComputedValues().opacity() > 0.0F) {
      boxOrder.emplace(&element, order++);
      boxes.push_back(&element);
    }
    for (int i = 0; i < element.GetNumChildren(); ++i) {
      self(*element.GetChild(i));
    }
  };
  walk(document);

  // A text under an opaque box drawn after it (one window over another) is hidden, not out of
  // place.
  const auto covered = [&](const SeenText& text) {
    return std::ranges::any_of(boxes, [&](Rml::Element* box) {
      return opaque(*box) && boxOrder.at(box) > text.order && !isAncestor(*box, text.owner) &&
             overlapOf(text.extent, borderRect(*box)) > tolerance;
    });
  };
  // A box drawn before an opaque box round the text lies under it, out of the text's way.
  const auto underTextBox = [&](const SeenText& text, Rml::Element& box) {
    for (Rml::Element* element = text.owner; element != nullptr;
         element = element->GetParentNode()) {
      const auto found = boxOrder.find(element);
      if (found != boxOrder.end() && opaque(*element) && found->second > boxOrder.at(&box) &&
          !isAncestor(*element, &box)) {
        return true;
      }
    }
    return false;
  };
  std::erase_if(texts, covered);
  // A row a list has scrolled out of its box, wholly or in part, is not on screen: the player
  // scrolls to it, and it is checked then. Past the sides it is still out of place.
  const auto scrolledOut = [&](const SeenText& text) {
    for (Rml::Element* element = text.owner; element != nullptr && element != &document;
         element = element->GetParentNode()) {
      if (scrollsVertically(*element)) {
        const Rect inside = paddingRect(*element);
        if (inside.top - text.extent.top > tolerance ||
            text.extent.bottom - inside.bottom > tolerance) {
          return true;
        }
      }
    }
    return false;
  };
  std::erase_if(texts, scrolledOut);
  // The part of a box a list shows: what it has scrolled out of its box is not drawn, so a text
  // under the list (a description) does not cross it.
  const auto shownPart = [&](Rml::Element& box) -> std::optional<Rect> {
    Rect rect = borderRect(box);
    for (Rml::Element* element = box.GetParentNode(); element != nullptr && element != &document;
         element = element->GetParentNode()) {
      if (scrollsVertically(*element)) {
        const Rect inside = paddingRect(*element);
        rect.top = std::max(rect.top, inside.top);
        rect.bottom = std::min(rect.bottom, inside.bottom);
      }
    }
    if (rect.bottom <= rect.top) {
      return std::nullopt;
    }
    return rect;
  };

  const auto report = [&](const SeenText& text, std::string against, float by) {
    found.push_back({.document = std::string{name},
                     .element = pathOf(*text.owner),
                     .text = text.shown,
                     .box = std::move(against),
                     .overflow = by / dpRatio});
  };
  for (std::size_t i = 0; i < texts.size(); ++i) {
    const SeenText& text = texts[i];
    // Out of the box round it, past a clip or off the window.
    if (auto [worst, against] = worstContainer(text, document, window); worst > tolerance) {
      report(text, std::move(against), worst);
      continue;
    }
    // Across the edge of a box drawn beside it (a label over its value's box).
    bool crossed = false;
    for (Rml::Element* box : boxes) {
      // The box round the text, and the parts of a frame round it, are checked above.
      Rml::Element* parent = box->GetParentNode();
      if (isAncestor(*box, text.owner) ||
          (parent != nullptr && isAncestor(*parent, text.owner) && framedByParts(*parent)) ||
          underTextBox(text, *box)) {
        continue;
      }
      const std::optional<Rect> shown = shownPart(*box);
      if (!shown) {
        continue;
      }
      const Rect rect = *shown;
      const float overlap = overlapOf(text.extent, rect);
      if (overlap > tolerance && overflowOf(text.extent, rect) > tolerance) {
        report(text, "across the edge of " + pathOf(*box), overlap);
        crossed = true;
        break;
      }
    }
    if (crossed) {
      continue;
    }
    // Over another text.
    for (std::size_t j = i + 1; j < texts.size(); ++j) {
      const float overlap = overlapOf(text.extent, texts[j].extent);
      if (overlap > tolerance) {
        report(text,
               std::format("over the text '{}' ({})", texts[j].shown, pathOf(*texts[j].owner)),
               overlap);
        break;
      }
    }
  }
  return found;
}

std::vector<Ui::Rect> findDrawnRects(Rml::Element& root, float windowWidth, float windowHeight,
                                     Rml::Element* leftOut) {
  std::vector<Ui::Rect> found;
  if (!root.IsVisible(true)) {
    return found;
  }
  const std::optional<Rect> leftOutRect =
      leftOut != nullptr ? std::optional{borderRect(*leftOut)} : std::nullopt;
  // A box round the element left out (its panel) holds it; one beside it does not.
  const auto holdsLeftOut = [&](Rml::Element& element) {
    if (!leftOutRect || !isAncestor(element, leftOut)) {
      return false;
    }
    const Rect rect = borderRect(element);
    return rect.left <= leftOutRect->left && rect.top <= leftOutRect->top &&
           leftOutRect->right <= rect.right && leftOutRect->bottom <= rect.bottom;
  };
  const Rect window{.left = 0.0F, .top = 0.0F, .right = windowWidth, .bottom = windowHeight};
  // A layer over half the window or more dims the game (a scrim); it is not a window.
  const float layerArea = 0.5F * windowWidth * windowHeight;
  const auto add = [&](const Rect& rect) {
    const Rect inside{.left = std::max(rect.left, window.left),
                      .top = std::max(rect.top, window.top),
                      .right = std::min(rect.right, window.right),
                      .bottom = std::min(rect.bottom, window.bottom)};
    const float width = inside.right - inside.left;
    const float height = inside.bottom - inside.top;
    if (width <= 0.0F || height <= 0.0F || width * height >= layerArea) {
      return;
    }
    found.push_back(
        {.left = inside.left, .top = inside.top, .right = inside.right, .bottom = inside.bottom});
  };
  const auto walk = [&](this const auto& self, Rml::Element& element) -> void {
    if (&element == leftOut || !element.IsVisible() ||
        element.GetComputedValues().has_local_transform() ||
        element.GetComputedValues().opacity() <= 0.0F) {
      return;
    }
    if (auto* text = rmlui_dynamic_cast<Rml::ElementText*>(&element)) {
      if (auto seenText = textOf(*text, windowWidth, windowHeight)) {
        add(seenText->extent);
      }
    } else if ((drawsItself(element) || element.GetTagName() == "img") && !holdsLeftOut(element)) {
      add(borderRect(element));
    }
    for (int i = 0; i < element.GetNumChildren(); ++i) {
      self(*element.GetChild(i));
    }
  };
  walk(root);
  return found;
}

} // namespace dwe::ui
