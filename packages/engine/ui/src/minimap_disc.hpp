#pragma once

#include <dwe/ui/minimap.hpp>

#include <RmlUi/Core/Decorator.h>

#include <string>
#include <vector>

namespace dwe::ui {

// The RCSS name of the round minimap (an engine improvement; the original has none):
// `decorator: minimap-disc(<ring-style> <fill> <ring-color> <glow-color> <ring-width>
// <glow-width>)`. It fills the element's box with a disc of the current map (MinimapDiscState) on
// `fill`, and draws the ring and its glow outside that disc, so the element's box is exactly the
// map's disc and the markers inside it are placed in percent of it. ring-style `plain` is one ring
// with a soft glow outside and a faint shade inside; `original` is four stripes of ring-width each
// in the original box's border colours (ui.c: renderUIBoxBorder), black, light cyan, dark cyan and
// black from the inside out.
inline constexpr const char* MinimapDiscDecorator = "minimap-disc";

// What the round minimap shows now: written by the UI when the map changes and every frame the
// player moves, read by every minimap-disc decorator when it draws.
struct MinimapDiscState {
  // The map image's texture source; empty while there is no map.
  std::string source;
  MinimapDiscView view;
};

enum class MinimapRingStyle : unsigned char { Plain, Original };

// One band of the ring, from `inner` to `outer` pixels out of the map's rim (negative is inside
// it), with a colour at each edge as straight 0xRRGGBBAA that fades between them.
struct MinimapRingBand {
  float inner = 0.0F;
  float outer = 0.0F;
  unsigned innerColour = 0;
  unsigned outerColour = 0;
};

// The bands of a ring in pixels: `ring` and `glow` are straight 0xRRGGBBAA colours.
[[nodiscard]] std::vector<MinimapRingBand> minimapRingBands(MinimapRingStyle style, unsigned ring,
                                                            unsigned glow, float ringWidth,
                                                            float glowWidth);

class MinimapDiscInstancer final : public Rml::DecoratorInstancer {
public:
  // The state must outlive the instancer and every decorator it makes.
  explicit MinimapDiscInstancer(const MinimapDiscState& state);

  Rml::SharedPtr<Rml::Decorator>
  InstanceDecorator(const Rml::String& name, const Rml::PropertyDictionary& properties,
                    const Rml::DecoratorInstancerInterface& instancer) override;

private:
  const MinimapDiscState* state_;
  Rml::PropertyId ringStyle_;
  Rml::PropertyId fill_;
  Rml::PropertyId ringColour_;
  Rml::PropertyId glowColour_;
  Rml::PropertyId ringWidth_;
  Rml::PropertyId glowWidth_;
};

} // namespace dwe::ui
