#include "minimap_disc.hpp"

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <RmlUi/Core/PropertyDictionary.h>
#include <RmlUi/Core/RenderManager.h>
#include <RmlUi/Core/Texture.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <utility>

namespace dwe::ui {
namespace {

// Segments round the disc: smooth at every size the HUD offers.
constexpr int Segments = 96;

// The original box's border colours (original_box.cpp), as straight 0xRRGGBBAA.
constexpr unsigned OriginalBlack = 0x020202FFU;
constexpr unsigned OriginalLight = 0x90D9FAFFU;
constexpr unsigned OriginalDark = 0x4A9FC5FFU;

// How far the plain ring's shade reaches inside the map, in ring widths, and how strong it is.
constexpr float ShadeWidths = 2.5F;
constexpr unsigned ShadeAlpha = 0x70U;

Rml::ColourbPremultiplied premultiplied(unsigned rgba, float opacity) {
  return Rml::Colourb{
      static_cast<Rml::byte>((rgba >> 24U) & 0xFFU), static_cast<Rml::byte>((rgba >> 16U) & 0xFFU),
      static_cast<Rml::byte>((rgba >> 8U) & 0xFFU), static_cast<Rml::byte>(rgba & 0xFFU)}
      .ToPremultiplied(opacity);
}

unsigned rgbaOf(const Rml::Colourb& colour) {
  return (unsigned{colour.red} << 24U) | (unsigned{colour.green} << 16U) |
         (unsigned{colour.blue} << 8U) | unsigned{colour.alpha};
}

unsigned withAlpha(unsigned rgba, unsigned alpha) {
  return (rgba & 0xFFFFFF00U) | (alpha & 0xFFU);
}

Rml::Vector2f onCircle(Rml::Vector2f centre, float radius, int segment) {
  const float angle =
      2.0F * std::numbers::pi_v<float> * static_cast<float>(segment) / static_cast<float>(Segments);
  return {centre.x + (radius * std::cos(angle)), centre.y + (radius * std::sin(angle))};
}

// A filled disc as a fan round its centre.
void addDisc(Rml::Mesh& mesh, Rml::Vector2f centre, float radius,
             Rml::ColourbPremultiplied colour) {
  const int first = static_cast<int>(mesh.vertices.size());
  mesh.vertices.push_back({.position = centre, .colour = colour, .tex_coord = {0.0F, 0.0F}});
  for (int segment = 0; segment < Segments; ++segment) {
    mesh.vertices.push_back(
        {.position = onCircle(centre, radius, segment), .colour = colour, .tex_coord = {}});
  }
  for (int segment = 0; segment < Segments; ++segment) {
    mesh.indices.insert(mesh.indices.end(),
                        {first, first + 1 + segment, first + 1 + ((segment + 1) % Segments)});
  }
}

void addBand(Rml::Mesh& mesh, Rml::Vector2f centre, float rim, const MinimapRingBand& band,
             float opacity) {
  const float inner = std::max(0.0F, rim + band.inner);
  const float outer = std::max(inner, rim + band.outer);
  if (outer <= inner) {
    return;
  }
  const Rml::ColourbPremultiplied innerColour = premultiplied(band.innerColour, opacity);
  const Rml::ColourbPremultiplied outerColour = premultiplied(band.outerColour, opacity);
  const int first = static_cast<int>(mesh.vertices.size());
  for (int segment = 0; segment < Segments; ++segment) {
    mesh.vertices.push_back(
        {.position = onCircle(centre, inner, segment), .colour = innerColour, .tex_coord = {}});
    mesh.vertices.push_back(
        {.position = onCircle(centre, outer, segment), .colour = outerColour, .tex_coord = {}});
  }
  for (int segment = 0; segment < Segments; ++segment) {
    const int a = first + (2 * segment);
    const int b = first + (2 * ((segment + 1) % Segments));
    mesh.indices.insert(mesh.indices.end(), {a, a + 1, b + 1, a, b + 1, b});
  }
}

// The map: a disc whose texture coordinates are the view's window on the map image. Outside the
// image the sampler clamps to its edge cells, which are blocked and so transparent.
Rml::Mesh mapMesh(Rml::Vector2f centre, float radius, const MinimapDiscView& view, float opacity) {
  Rml::Mesh mesh;
  const Rml::ColourbPremultiplied white = Rml::Colourb{255, 255, 255, 255}.ToPremultiplied(opacity);
  mesh.vertices.push_back(
      {.position = centre, .colour = white, .tex_coord = {view.centre.u, view.centre.v}});
  for (int segment = 0; segment < Segments; ++segment) {
    const float angle = 2.0F * std::numbers::pi_v<float> * static_cast<float>(segment) /
                        static_cast<float>(Segments);
    mesh.vertices.push_back({.position = onCircle(centre, radius, segment),
                             .colour = white,
                             .tex_coord = {view.centre.u + (view.radiusU * std::cos(angle)),
                                           view.centre.v + (view.radiusV * std::sin(angle))}});
  }
  for (int segment = 0; segment < Segments; ++segment) {
    mesh.indices.insert(mesh.indices.end(), {0, 1 + segment, 1 + ((segment + 1) % Segments)});
  }
  return mesh;
}

struct Params {
  MinimapRingStyle style = MinimapRingStyle::Plain;
  unsigned fill = 0;
  unsigned ring = 0;
  unsigned glow = 0;
  Rml::NumericValue ringWidth;
  Rml::NumericValue glowWidth;
};

struct ElementData {
  Rml::Geometry fill;
  Rml::Geometry ring;
  Rml::Vector2f centre;
  float radius = 0.0F;
  // The map's disc, made again when the view, the map or the opacity changes.
  Rml::Geometry map;
  std::string source;
  Rml::Texture texture;
  MinimapDiscView view;
  float opacity = -1.0F;
};

class MinimapDisc final : public Rml::Decorator {
public:
  MinimapDisc(const MinimapDiscState& state, Params params)
      : state_{&state}, params_{std::move(params)} {}

  Rml::DecoratorDataHandle GenerateElementData(Rml::Element* element,
                                               Rml::BoxArea paintArea) const override {
    Rml::RenderManager* render = element->GetRenderManager();
    if (render == nullptr) {
      return Rml::Decorator::INVALID_DECORATORDATAHANDLE;
    }
    const Rml::Vector2f origin = element->GetBox().GetPosition(paintArea);
    const Rml::Vector2f size = element->GetBox().GetSize(paintArea);
    const float radius = std::min(size.x, size.y) / 2.0F;
    if (radius <= 0.0F) {
      return Rml::Decorator::INVALID_DECORATORDATAHANDLE;
    }
    const Rml::Vector2f centre{origin.x + (size.x / 2.0F), origin.y + (size.y / 2.0F)};
    const float opacity = element->GetComputedValues().opacity();
    Rml::Mesh fill;
    addDisc(fill, centre, radius, premultiplied(params_.fill, opacity));
    Rml::Mesh ring;
    for (const MinimapRingBand& band :
         minimapRingBands(params_.style, params_.ring, params_.glow,
                          std::max(1.0F, std::round(element->ResolveLength(params_.ringWidth))),
                          std::max(0.0F, element->ResolveLength(params_.glowWidth)))) {
      addBand(ring, centre, radius, band, opacity);
    }
    auto* data = new ElementData{};
    data->fill = render->MakeGeometry(std::move(fill));
    data->ring = render->MakeGeometry(std::move(ring));
    data->centre = centre;
    data->radius = radius;
    return reinterpret_cast<Rml::DecoratorDataHandle>(data);
  }

  void ReleaseElementData(Rml::DecoratorDataHandle handle) const override {
    delete reinterpret_cast<ElementData*>(handle);
  }

  void RenderElement(Rml::Element* element, Rml::DecoratorDataHandle handle) const override {
    auto* data = reinterpret_cast<ElementData*>(handle);
    const Rml::Vector2f offset = element->GetAbsoluteOffset(Rml::BoxArea::Border);
    data->fill.Render(offset);
    if (!state_->source.empty()) {
      if (Rml::RenderManager* render = element->GetRenderManager(); render != nullptr) {
        if (data->source != state_->source) {
          data->source = state_->source;
          data->texture = render->LoadTexture(data->source);
          data->opacity = -1.0F;
        }
        const float opacity = element->GetComputedValues().opacity();
        const MinimapDiscView& view = state_->view;
        if (opacity != data->opacity || view.centre.u != data->view.centre.u ||
            view.centre.v != data->view.centre.v || view.radiusU != data->view.radiusU ||
            view.radiusV != data->view.radiusV) {
          data->opacity = opacity;
          data->view = view;
          data->map = render->MakeGeometry(mapMesh(data->centre, data->radius, view, opacity));
        }
        data->map.Render(offset, data->texture);
      }
    }
    data->ring.Render(offset);
  }

private:
  const MinimapDiscState* state_;
  Params params_;
};

} // namespace

std::vector<MinimapRingBand> minimapRingBands(MinimapRingStyle style, unsigned ring, unsigned glow,
                                              float ringWidth, float glowWidth) {
  std::vector<MinimapRingBand> bands;
  if (style == MinimapRingStyle::Original) {
    // From the inside out, as the original box's left edge from its inside: black, light, dark,
    // black.
    float at = 0.0F;
    for (const unsigned colour : {OriginalBlack, OriginalLight, OriginalDark, OriginalBlack}) {
      bands.push_back(
          {.inner = at, .outer = at + ringWidth, .innerColour = colour, .outerColour = colour});
      at += ringWidth;
    }
    if (glowWidth > 0.0F) {
      bands.push_back({.inner = at,
                       .outer = at + glowWidth,
                       .innerColour = glow,
                       .outerColour = withAlpha(glow, 0)});
    }
    return bands;
  }
  bands.push_back({.inner = -ShadeWidths * ringWidth,
                   .outer = 0.0F,
                   .innerColour = withAlpha(ring, 0),
                   .outerColour = withAlpha(ring, std::min(ShadeAlpha, ring & 0xFFU))});
  bands.push_back({.inner = 0.0F, .outer = ringWidth, .innerColour = ring, .outerColour = ring});
  if (glowWidth > 0.0F) {
    bands.push_back({.inner = ringWidth,
                     .outer = ringWidth + glowWidth,
                     .innerColour = glow,
                     .outerColour = withAlpha(glow, 0)});
  }
  return bands;
}

MinimapDiscInstancer::MinimapDiscInstancer(const MinimapDiscState& state) : state_{&state} {
  ringStyle_ =
      RegisterProperty("ring-style", "plain").AddParser("keyword", "plain, original").GetId();
  fill_ = RegisterProperty("fill", "#0d121ad0").AddParser("color").GetId();
  ringColour_ = RegisterProperty("ring-color", "#8fd8ff").AddParser("color").GetId();
  glowColour_ = RegisterProperty("glow-color", "#3aa8ff80").AddParser("color").GetId();
  ringWidth_ = RegisterProperty("ring-width", "3dp").AddParser("length").GetId();
  glowWidth_ = RegisterProperty("glow-width", "8dp").AddParser("length").GetId();
  RegisterShorthand("decorator", "ring-style, fill, ring-color, glow-color, ring-width, glow-width",
                    Rml::ShorthandType::FallThrough);
}

Rml::SharedPtr<Rml::Decorator>
MinimapDiscInstancer::InstanceDecorator(const Rml::String& /*name*/,
                                        const Rml::PropertyDictionary& properties,
                                        const Rml::DecoratorInstancerInterface& /*instancer*/) {
  const Rml::Property* style = properties.GetProperty(ringStyle_);
  const Rml::Property* fill = properties.GetProperty(fill_);
  const Rml::Property* ring = properties.GetProperty(ringColour_);
  const Rml::Property* glow = properties.GetProperty(glowColour_);
  const Rml::Property* ringWidth = properties.GetProperty(ringWidth_);
  const Rml::Property* glowWidth = properties.GetProperty(glowWidth_);
  if (style == nullptr || fill == nullptr || ring == nullptr || glow == nullptr ||
      ringWidth == nullptr || glowWidth == nullptr) {
    return nullptr;
  }
  return Rml::MakeShared<MinimapDisc>(
      *state_,
      Params{.style = style->Get<int>() == 1 ? MinimapRingStyle::Original : MinimapRingStyle::Plain,
             .fill = rgbaOf(fill->Get<Rml::Colourb>()),
             .ring = rgbaOf(ring->Get<Rml::Colourb>()),
             .glow = rgbaOf(glow->Get<Rml::Colourb>()),
             .ringWidth = ringWidth->GetNumericValue(),
             .glowWidth = glowWidth->GetNumericValue()});
}

} // namespace dwe::ui
