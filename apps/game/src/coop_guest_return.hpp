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

inline constexpr std::string_view CoopReturnSection = "coop_return";
inline constexpr std::int32_t CoopReturnVersion = 1;

struct CoopReturnNote {
  std::string host;
  std::string species;

  friend bool operator==(const CoopReturnNote&, const CoopReturnNote&) = default;
};

[[nodiscard]] game::save::SavedSection coopReturnSection(const CoopReturnNote& note);
[[nodiscard]] std::optional<CoopReturnNote>
coopReturnNoteOf(const game::save::SavedSection& section);

[[nodiscard]] core::Result<void> checkReturningPartner(const game::partner::PartnerState& partner);

[[nodiscard]] core::Result<game::save::SaveFile>
coopGuestReturn(const game::save::SaveFile& home, const game::save::SaveFile& brought,
                const game::save::SaveFile& played, std::string_view host,
                std::chrono::sys_seconds now);

enum class CoopReturnChange : std::uint8_t { None, Partner, Bag };
[[nodiscard]] CoopReturnChange coopReturnChange(const game::save::SaveFile& last,
                                                const game::save::SaveFile& played);

class CoopGuestHome {
public:
  static constexpr double PartnerEverySeconds = 30.0;

  struct Home {
    game::save::SlotId slot;
    bool backup = false;
    std::string text;
  };

  core::Result<void> begin(Home home, std::string host, double seconds);
  [[nodiscard]] bool active() const { return home_.has_value(); }
  [[nodiscard]] const std::optional<Home>& home() const { return home_; }
  [[nodiscard]] const std::string& host() const { return host_; }

  [[nodiscard]] bool due(const game::save::SaveFile& played, double seconds) const;
  [[nodiscard]] bool changed(const game::save::SaveFile& played) const;
  void failed(double seconds) { failedAt_ = seconds; }

  core::Result<std::string> bringHome(game::save::SaveStorage& storage,
                                      const game::save::SaveSettings& settings,
                                      const game::save::SaveFile& played,
                                      std::chrono::sys_seconds now, double seconds);

  void end();

private:
  std::optional<Home> home_;
  std::string host_;
  std::optional<game::save::SaveFile> brought_;
  std::optional<game::save::SaveFile> written_;
  double writtenAt_ = 0.0;
  std::optional<double> failedAt_;
  bool keptBefore_ = false;
};

} // namespace dwe::app
