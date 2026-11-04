#pragma once

#include <dwe/screens/damage_numbers.hpp>
#include <dwe/screens/field_hud_view.hpp>
#include <dwe/ui/ui.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace dwe::app {

// The document of the numbers over the partner on the field.
inline constexpr std::string_view FieldNumbersDocument = "field_numbers";

// The numbers that pop out of the partner on the field: the damage a script's deal_damage does
// (script_interp.c:1989: addEntityText(partner, 0, 0, damage, 0)), as Elecmon's shock shows it.
// entity_text.c: setCombatTextPosition fixes a number where the partner's feet showed when it was
// added (the field's GAME_STATE is not the versus mode's 4, so it does not follow the partner) and
// renderEntityText moves it once a field frame, the same bounce and queue as the battle's
// (screens::DamageNumbers); the document shows while a number is up.
class FieldNumbers {
public:
  // `frameSeconds`: one field frame (field rules: transition.frame_seconds, 1/20 s).
  FieldNumbers(ui::Ui& ui, float frameSeconds);
  ~FieldNumbers();
  FieldNumbers(const FieldNumbers&) = delete;
  FieldNumbers& operator=(const FieldNumbers&) = delete;
  FieldNumbers(FieldNumbers&&) = delete;
  FieldNumbers& operator=(FieldNumbers&&) = delete;

  // Queues each damage over the partner, whose feet show at `feet` now (nothing when off the
  // camera). The frame a number is added draws it.
  void add(std::span<const std::int32_t> damage, std::optional<screens::WindowFraction> feet);
  // One field tick of `seconds`: every number moves on once each field frame.
  void tick(double seconds);
  // Takes every number away (a battle or another screen replaces the field).
  void clear();

  [[nodiscard]] bool active() const { return !numbers_.empty() || !numbers_.shown().empty(); }
  // The numbers the last field frame drew.
  [[nodiscard]] const std::vector<screens::ShownNumber>& shown() const { return numbers_.shown(); }

private:
  void step();
  void publish();

  ui::Ui* ui_;
  double frameSeconds_;
  double seconds_ = 0.0;
  screens::DamageNumbers numbers_;
  bool visible_ = false;
};

} // namespace dwe::app
