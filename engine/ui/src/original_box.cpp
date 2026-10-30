#include "original_box.hpp"

#include <RmlUi/Core/ComputedValues.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/Geometry.h>
#include <RmlUi/Core/Mesh.h>
#include <RmlUi/Core/MeshUtilities.h>
#include <RmlUi/Core/PropertyDefinition.h>
#include <RmlUi/Core/PropertyDictionary.h>
#include <RmlUi/Core/RenderManager.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>

namespace dwe::ui {
namespace {

// renderUIBoxBorder's colours (BGR words there): black 0x20202, "gold" 0xFAD990 and "brown"
// 0xC59F4A, which are a light and a dark cyan.
constexpr unsigned Black = 0x020202;
constexpr unsigned Light = 0x90D9FA;
constexpr unsigned Dark = 0x4A9FC5;

Rml::ColourbPremultiplied colourOf(unsigned rgb, float opacity) {
  return Rml::Colourb{static_cast<Rml::byte>((rgb >> 16U) & 0xFFU),
                      static_cast<Rml::byte>((rgb >> 8U) & 0xFFU),
                      static_cast<Rml::byte>(rgb & 0xFFU), 255}
      .ToPremultiplied(opacity);
}

struct ElementData {
  Rml::Geometry geometry;
};

class OriginalBox final : public Rml::Decorator {
public:
  explicit OriginalBox(Rml::NumericValue unit) : unit_{unit} {}

  Rml::DecoratorDataHandle GenerateElementData(Rml::Element* element,
                                               Rml::BoxArea paintArea) const override {
    Rml::RenderManager* render = element->GetRenderManager();
    if (render == nullptr) {
      return Rml::Decorator::INVALID_DECORATORDATAHANDLE;
    }
    // Whole device pixels per original pixel, so the lines stay sharp at any window size.
    const float pixel = std::max(1.0F, std::round(element->ResolveLength(unit_)));
    const Rml::Vector2f origin = element->GetBox().GetPosition(paintArea);
    const Rml::Vector2f size = element->GetBox().GetSize(paintArea);
    const int width = static_cast<int>(std::round(size.x / pixel));
    const int height = static_cast<int>(std::round(size.y / pixel));
    const float opacity = element->GetComputedValues().opacity();
    Rml::Mesh mesh;
    for (const OriginalBoxLine& line : originalBoxLines(width, height)) {
      const Rml::Vector2f at{origin.x + static_cast<float>(line.x) * pixel,
                             origin.y + static_cast<float>(line.y) * pixel};
      const float length = static_cast<float>(line.length) * pixel;
      Rml::MeshUtilities::GenerateQuad(
          mesh, at, line.horizontal ? Rml::Vector2f{length, pixel} : Rml::Vector2f{pixel, length},
          colourOf(line.colour, opacity));
    }
    if (!mesh) {
      return Rml::Decorator::INVALID_DECORATORDATAHANDLE;
    }
    auto* data = new ElementData{render->MakeGeometry(std::move(mesh))};
    return reinterpret_cast<Rml::DecoratorDataHandle>(data);
  }

  void ReleaseElementData(Rml::DecoratorDataHandle handle) const override {
    delete reinterpret_cast<ElementData*>(handle);
  }

  void RenderElement(Rml::Element* element, Rml::DecoratorDataHandle handle) const override {
    reinterpret_cast<ElementData*>(handle)->geometry.Render(
        element->GetAbsoluteOffset(Rml::BoxArea::Border));
  }

private:
  Rml::NumericValue unit_;
};

} // namespace

Rml::Vector<OriginalBoxLine> originalBoxLines(int width, int height) {
  // Too small for the corners: nothing to draw.
  if (width < 9 || height < 8) {
    return {};
  }
  const int across = width - 7; // x + 4 to x + w - 4, both drawn (drawLine2P).
  const int down = height - 6;  // y + 4 to y + h - 3.
  return {
      // The top and the bottom: black, light, black.
      {.x = 4, .y = 0, .length = across, .horizontal = true, .colour = Black},
      {.x = 4, .y = 1, .length = across, .horizontal = true, .colour = Light},
      {.x = 4, .y = 2, .length = across, .horizontal = true, .colour = Black},
      {.x = 4, .y = height - 3, .length = across, .horizontal = true, .colour = Black},
      {.x = 4, .y = height - 2, .length = across, .horizontal = true, .colour = Light},
      {.x = 4, .y = height - 1, .length = across, .horizontal = true, .colour = Black},
      // The left: black, dark, light, then renderUIBoxStatic's black inner line from y + 3.
      {.x = 0, .y = 4, .length = down, .horizontal = false, .colour = Black},
      {.x = 1, .y = 4, .length = down, .horizontal = false, .colour = Dark},
      {.x = 2, .y = 4, .length = down, .horizontal = false, .colour = Light},
      {.x = 3, .y = 3, .length = down + 1, .horizontal = false, .colour = Black},
      // The right: renderUIBoxStatic's black inner line, then light, dark, black.
      {.x = width - 4, .y = 3, .length = down + 1, .horizontal = false, .colour = Black},
      {.x = width - 3, .y = 4, .length = down, .horizontal = false, .colour = Light},
      {.x = width - 2, .y = 4, .length = down, .horizontal = false, .colour = Dark},
      {.x = width - 1, .y = 4, .length = down, .horizontal = false, .colour = Black},
  };
}

OriginalBoxInstancer::OriginalBoxInstancer() {
  unit_ = RegisterProperty("unit", "3dp").AddParser("length").GetId();
  RegisterShorthand("decorator", "unit", Rml::ShorthandType::FallThrough);
}

Rml::SharedPtr<Rml::Decorator>
OriginalBoxInstancer::InstanceDecorator(const Rml::String& /*name*/,
                                        const Rml::PropertyDictionary& properties,
                                        const Rml::DecoratorInstancerInterface& /*instancer*/) {
  const Rml::Property* unit = properties.GetProperty(unit_);
  if (unit == nullptr) {
    return nullptr;
  }
  return Rml::MakeShared<OriginalBox>(unit->GetNumericValue());
}

} // namespace dwe::ui
