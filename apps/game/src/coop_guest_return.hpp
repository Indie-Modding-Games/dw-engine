#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/partner/state.hpp>
#include <dwe/game/save/save_file.hpp>
#include <dwe/game/save/settings.hpp>
#include <dwe/game/save/slots.hpp>
#include <dwe/game/save/storage.hpp>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace dwe::app {

// A co-op guest takes its progress home (docs/architecture/multiplayer.md, "Saves in co-op",
// decision 4): its game plays with its own partner, moves, raised species and bag
// (CoopGuestOwnSections), and those go back into the slot it brought them from. The host's world
// (story, triggers, map, money of the host's bag, the clock) never does: the rest of the slot
// stays as it was on disk.

// The section a return leaves in the guest's save: whose world it came back from. No system reads
// it: the game shows it once as the save loads, and the next save drops it
// (GameSession::takeUnreadSection).
inline constexpr std::string_view CoopReturnSection = "coop_return";
inline constexpr std::int32_t CoopReturnVersion = 1;

// What the note says.
struct CoopReturnNote {
  // The host's name as the guest's link heard it; empty when the host had none.
  std::string host;
  // The partner's species as it came home.
  std::string species;

  friend bool operator==(const CoopReturnNote&, const CoopReturnNote&) = default;
};

[[nodiscard]] game::save::SavedSection coopReturnSection(const CoopReturnNote& note);
// The note of a save's section; nothing when it is not one this game wrote.
[[nodiscard]] std::optional<CoopReturnNote>
coopReturnNoteOf(const game::save::SavedSection& section);

// Whether a partner is one a game can leave: a species, every stat within its limit (9999 HP and
// MP, 999 the rest: evl.c EVL_clampBaseStats), HP and MP not above them, and no negative lives,
// lifetime, age or weight. A return writes nothing otherwise.
[[nodiscard]] core::Result<void> checkReturningPartner(const game::partner::PartnerState& partner);

// The guest's save with what its co-op game brings home. `home` is the slot as it is on disk now,
// `brought` the save the guest brought into the room (the same playthrough, or nothing is written:
// the slot was taken by another game meanwhile), `played` the guest's game now. The result is
// `home` with the guest's own sections taken from `played`, its summary's partner, and the note.
[[nodiscard]] core::Result<game::save::SaveFile>
coopGuestReturn(const game::save::SaveFile& home, const game::save::SaveFile& brought,
                const game::save::SaveFile& played, std::string_view host,
                std::chrono::sys_seconds now);

// What changed in the guest's own sections since `last`: nothing, the partner's growth (written
// at the next safe point a while later) or the bag (written at once: a trade or a claim moved an
// item, and a crash must not leave it in two games).
enum class CoopReturnChange : std::uint8_t { None, Partner, Bag };
[[nodiscard]] CoopReturnChange coopReturnChange(const game::save::SaveFile& last,
                                                const game::save::SaveFile& played);

// The guest's side of a return: the slot it brought, and when to write it. The app gives it the
// guest's game as a save at safe points (nothing busy, PlaySession::coopGuestSave) and writes it
// home when due and when the game ends, whatever ends it (leaving, the host closing the room, a
// drop, quitting the game).
class CoopGuestHome {
public:
  // A growth-only change waits this long after the last write, so a long game loses little to a
  // crash without writing the slot every few seconds.
  static constexpr double PartnerEverySeconds = 30.0;

  // Where the guest's save came from: its slot (or the slot's backup it was read from) and the
  // text it brought into the room.
  struct Home {
    game::save::SlotId slot;
    bool backup = false;
    std::string text;
  };

  // A guest's game starts at `seconds` with the save it brought from `home`, in `host`'s world:
  // the partner's growth is first written a while after it, not as the game starts.
  core::Result<void> begin(Home home, std::string host, double seconds);
  [[nodiscard]] bool active() const { return home_.has_value(); }
  [[nodiscard]] const std::optional<Home>& home() const { return home_; }
  [[nodiscard]] const std::string& host() const { return host_; }

  // Whether `played` (the guest's game at a safe point, `seconds` into the app's clock) should be
  // written home now.
  [[nodiscard]] bool due(const game::save::SaveFile& played, double seconds) const;
  // Whether `played` holds anything not written home yet: what the game's end writes.
  [[nodiscard]] bool changed(const game::save::SaveFile& played) const;
  // A write failed at `seconds`: the next is not due before PartnerEverySeconds (the game's end
  // still tries).
  void failed(double seconds) { failedAt_ = seconds; }

  // Writes `played` home: re-reads the slot, checks it is still the brought playthrough, keeps
  // the save before the co-op game once (game::save::slotBeforeCoopName, until the next co-op
  // game's first return or the slot's erase) and saves the slot (SaveSlots::save
  // keeps the previous one as its backup). On success the brought save becomes the slot's new
  // text, which a guest coming back to the room brings (CoopScreen::broughtHome): its items are
  // never brought in twice.
  core::Result<std::string> bringHome(game::save::SaveStorage& storage,
                                      const game::save::SaveSettings& settings,
                                      const game::save::SaveFile& played,
                                      std::chrono::sys_seconds now, double seconds);

  // The game ended: nothing more is written.
  void end();

private:
  std::optional<Home> home_;
  std::string host_;
  std::optional<game::save::SaveFile> brought_;
  // What was written last (or brought), to tell what changed.
  std::optional<game::save::SaveFile> written_;
  double writtenAt_ = 0.0;
  std::optional<double> failedAt_;
  bool keptBefore_ = false;
};

} // namespace dwe::app
