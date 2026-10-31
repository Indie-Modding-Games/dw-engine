#pragma once

#include <dwe/ui/bitmap_font.hpp>
#include <dwe/ui/text_font.hpp>

#include <RmlUi/Core/CallbackTexture.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/FontMetrics.h>

#include <memory>
#include <optional>
#include <vector>

namespace dwe::ui {

// RmlUi's font engine with the "original look" family over it (ui/fonts/original_look.yaml). That
// family draws with the bitmap font or with the modern family, as the player's choice says; with
// the bitmap font, the characters it lacks are drawn with the modern family on the same baseline.
// Every other family goes to RmlUi's own engine (FreeType), which this one wraps and forwards to.
class FontEngine final : public Rml::FontEngineInterface {
public:
  // Without a bitmap font, the original choice draws with the modern family too.
  FontEngine(Rml::FontEngineInterface& freeType, OriginalLook look,
             std::optional<BitmapFont> bitmap);

  // Changes what the family draws with. Documents only see it once RmlUi releases its font
  // resources.
  void setTextFont(TextFont font) { textFont_ = font; }
  [[nodiscard]] TextFont textFont() const { return textFont_; }
  // The context's pixels per dp, which sets how many times larger the bitmap font draws.
  void setDpRatio(float ratio) { dpRatio_ = ratio; }

  void Initialize() override;
  void Shutdown() override;
  bool LoadFontFace(const Rml::String& fileName, int faceIndex, bool fallbackFace,
                    Rml::Style::FontWeight weight) override;
  bool LoadFontFace(const Rml::String& fileName, int faceIndex, const Rml::String& family,
                    Rml::Style::FontStyle style, Rml::Style::FontWeight weight,
                    bool fallbackFace) override;
  bool LoadFontFace(Rml::Span<const Rml::byte> data, int faceIndex, const Rml::String& family,
                    Rml::Style::FontStyle style, Rml::Style::FontWeight weight,
                    bool fallbackFace) override;
  Rml::FontFaceHandle GetFontFaceHandle(const Rml::String& family, Rml::Style::FontStyle style,
                                        Rml::Style::FontWeight weight, int size) override;
  Rml::FontEffectsHandle PrepareFontEffects(Rml::FontFaceHandle handle,
                                            const Rml::FontEffectList& fontEffects) override;
  const Rml::FontMetrics& GetFontMetrics(Rml::FontFaceHandle handle) override;
  int GetStringWidth(Rml::FontFaceHandle handle, Rml::StringView string,
                     const Rml::TextShapingContext& shaping,
                     Rml::Character priorCharacter) override;
  int GenerateString(Rml::RenderManager& renderManager, Rml::FontFaceHandle faceHandle,
                     Rml::FontEffectsHandle effectsHandle, Rml::StringView string,
                     Rml::Vector2f position, Rml::ColourbPremultiplied colour, float opacity,
                     const Rml::TextShapingContext& shaping,
                     Rml::TexturedMeshList& meshList) override;
  int GetVersion(Rml::FontFaceHandle handle) override;
  void ReleaseFontResources() override;

private:
  // The bitmap font at one size: drawn `scale` times larger, with the modern face that draws what
  // it lacks.
  struct BitmapFace {
    const BitmapFont* font = nullptr;
    Rml::Style::FontStyle style;
    Rml::Style::FontWeight weight;
    int size = 0;
    int scale = 1;
    Rml::FontFaceHandle fallback = 0;
    Rml::FontMetrics metrics{};
    Rml::Vector2f atlasSize;
    Rml::CallbackTextureSource texture;
  };

  [[nodiscard]] bool isOriginalLook(const Rml::String& family) const;
  Rml::FontFaceHandle bitmapFaceHandle(const BitmapFont& font, Rml::Style::FontStyle style,
                                       Rml::Style::FontWeight weight, int size);
  [[nodiscard]] static Rml::FontMetrics bitmapMetrics(const BitmapFont& font, int size, int scale);
  [[nodiscard]] BitmapFace* bitmapFace(Rml::FontFaceHandle handle) const;
  [[nodiscard]] static float bitmapRunWidth(const BitmapFace& face, Rml::StringView run,
                                            float letterSpacing);
  static void addBitmapRun(const BitmapFace& face, Rml::StringView run,
                           Rml::ColourbPremultiplied tint, float baseline, float letterSpacing,
                           float& penX, Rml::Mesh& mesh);

  Rml::FontEngineInterface* freeType_;
  OriginalLook look_;
  std::optional<BitmapFont> bitmap_;
  TextFont textFont_;
  float dpRatio_ = 1.0F;
  std::vector<std::unique_ptr<BitmapFace>> faces_;
};

} // namespace dwe::ui
