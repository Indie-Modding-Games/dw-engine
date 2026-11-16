#pragma once

#include <dwe/game/shop_job/shop_job.hpp>
#include <dwe/session/session_system.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>

namespace dwe::session {

class InventoryAdapter;
class StoryAdapter;

struct ShopJobAdapterDesc {
  // The story (its variables and the shops' tally) and the bag (the bonus item); they must outlive
  // the adapter.
  StoryAdapter* story = nullptr;
  InventoryAdapter* inventory = nullptr;
};

// What the player did at the counter (the haggle screen).
enum class ShopJobAction : std::uint8_t { Offer, Talk, Dismiss };

// How a counter turn ended, as the story patches read it from the result variable.
enum class ShopJobTurn : std::uint8_t { Sold, Refused, Talked, Left, Dismissed };

// The canyon shop's job, improved (game::shop_job; the option shop_job.mode, improved unless the
// player picks the original): the job's line of customers, made at its start from the
// playthrough's seed and the job's number and kept by that number in the save, so a load brings the
// same line back; the haggle with the customer at the counter; and what each turn writes for the
// story's patches (story/patches/shop_job_*.lua). Without the ruleset's shop_job section, or in
// the original mode, the story plays the original's scripts.
class ShopJobAdapter final : public SessionSystem {
public:
  static constexpr std::string_view SaveName = "shop_job";

  explicit ShopJobAdapter(ShopJobAdapterDesc desc);
  ~ShopJobAdapter() override;
  ShopJobAdapter(const ShopJobAdapter&) = delete;
  ShopJobAdapter& operator=(const ShopJobAdapter&) = delete;
  ShopJobAdapter(ShopJobAdapter&&) = delete;
  ShopJobAdapter& operator=(ShopJobAdapter&&) = delete;

  core::Result<void> readRules(const ruleset::Ruleset& rules) override;
  core::Result<void> addSaveSections(game::save::SaveSections& sections) override;
  core::Result<void> startNewGame(GameSession& session) override;
  void afterLoad(GameSession& session) override;

  // The option: the improved job (true, the default) or the original's.
  void setImproved(bool improved) { improved_ = improved; }
  // Whether the story plays the improved job: the option is on and the ruleset has its rules.
  [[nodiscard]] bool improved() const { return improved_ && rules_.has_value(); }
  [[nodiscard]] const game::shop_job::ShopJobRules* rules() const {
    return rules_ ? &*rules_ : nullptr;
  }

  // A new job: the next job's number and its line.
  void startJob(std::uint64_t gameSeed);
  // The job's line while one runs.
  [[nodiscard]] const game::shop_job::Lineup* lineup() const {
    return lineup_ ? &*lineup_ : nullptr;
  }
  // The customer at `index` (from 0) of the running job's line, nullptr past it.
  [[nodiscard]] const game::shop_job::Customer* customerAt(std::size_t index) const;
  // The customer at `index` comes to the counter; false past the line.
  bool beginCustomer(std::size_t index);
  // Whether a customer of the running line has come to the counter: a job that starts again
  // (customers served back at 0) after that one needs a new line.
  [[nodiscard]] bool lineBegun() const { return lineBegun_; }
  // The haggle with the customer at the counter, nullptr when none is there.
  [[nodiscard]] const game::shop_job::Haggle* haggle() const {
    return haggle_ ? &*haggle_ : nullptr;
  }
  [[nodiscard]] std::size_t customerIndex() const { return customerIndex_; }
  // Plays the player's turn at the counter; the story learns it with finishTurn. False when no
  // customer is there or the action cannot be taken (a second talk).
  bool play(ShopJobAction action, game::shop_job::Level level = 0);
  // Whether a turn was played and not yet told to the story.
  [[nodiscard]] bool turnPlayed() const { return turn_.has_value(); }
  // Writes the turn for the story: its result, the offer's level and face, and a sale's profit
  // into the shops' tally.
  core::Result<void> finishTurn();
  // The job's end: gives the bonus item when the tally reaches it. `earned` without `given`: the
  // bag had no room for it.
  struct Bonus {
    bool earned = false;
    bool given = false;
  };
  core::Result<Bonus> payBonus();

private:
  class Serializer;
  void makeLine(std::uint64_t gameSeed);

  ShopJobAdapterDesc desc_;
  std::optional<game::shop_job::ShopJobRules> rules_;
  bool improved_ = true;
  // Jobs started in the playthrough, and whether one runs.
  std::uint64_t jobs_ = 0;
  bool running_ = false;
  std::optional<game::shop_job::Lineup> lineup_;
  std::optional<game::shop_job::Haggle> haggle_;
  std::size_t customerIndex_ = 0;
  bool lineBegun_ = false;
  struct Turn {
    ShopJobTurn result = ShopJobTurn::Dismissed;
    game::shop_job::Level level = 0;
    std::int32_t face = 0;
    std::int64_t profit = 0;
  };
  std::optional<Turn> turn_;
  std::unique_ptr<Serializer> serializer_;
};

} // namespace dwe::session
