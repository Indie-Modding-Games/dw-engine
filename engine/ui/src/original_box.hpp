#pragma once

#include <RmlUi/Core/Decorator.h>

namespace dwe::ui {

// The RCSS name of the original box's border: `decorator: original-box border-box` (optionally
// `original-box(4dp)`, the size of one of the original's pixels; 3dp by default, its 240 lines over
// 720 dp).
inline constexpr const char* OriginalBoxDecorator = "original-box";

// The border of the original's windows (ui.c: renderUIBoxBorder and renderUIBoxStatic): one-pixel
// lines, black, light cyan and black along the top and the bottom; black, dark cyan, light cyan and
// black on the left; black, light cyan, dark cyan and black on the right. RCSS gives a border a
// single colour per side, so this decorator draws them. The rounded corners stay the skin's
// sprites, as image decorators after this one.
class OriginalBoxInstancer final : public Rml::DecoratorInstancer {
public:
  OriginalBoxInstancer();

  Rml::SharedPtr<Rml::Decorator>
  InstanceDecorator(const Rml::String& name, const Rml::PropertyDictionary& properties,
                    const Rml::DecoratorInstancerInterface& instancer) override;

private:
  Rml::PropertyId unit_;
};

// One line of the border in the original's pixels, from the border box's top left corner: where it
// starts, how long it is (horizontal when `horizontal`), and its colour as 0xRRGGBB.
struct OriginalBoxLine {
  int x = 0;
  int y = 0;
  int length = 0;
  bool horizontal = true;
  unsigned colour = 0;
};

// The lines of a box `width` x `height` original pixels, as renderUIBoxBorder and
// renderUIBoxStatic draw them.
[[nodiscard]] Rml::Vector<OriginalBoxLine> originalBoxLines(int width, int height);

} // namespace dwe::ui
