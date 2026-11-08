#pragma once

#include <dwe/core/error.hpp>
#include <dwe/game/battle/battle.hpp>
#include <dwe/game/versus/protocol.hpp>
#include <dwe/game/versus/series.hpp>

#include <array>
#include <cstdint>
#include <string>

namespace dwe::assets {
class VirtualFileSystem;
} // namespace dwe::assets

namespace dwe::ruleset {
class Ruleset;
} // namespace dwe::ruleset

namespace dwe::session {

struct RegisteredPartner;

// The glue between a versus series over the network (game::versus) and the battles it plays: what a
// player sends of a registered partner, what identifies the content both machines must share, and
// how a tick's two inputs and a battle's end become the lockstep's and the series' terms.

// A registered partner as it travels to the other machine (main_menu.c: registerBattleData's
// record, ids instead of numbers).
[[nodiscard]] game::versus::VersusFighter versusFighterOf(const RegisteredPartner& partner);

// What the battles read, hashed (SHA-1, in hex): the ruleset's id, version and chain, every
// section's YAML and every table's bytes. Two machines whose hashes differ would not play the
// same battles, so the host refuses the guest.
core::Result<std::string> versusContentHash(const ruleset::Ruleset& rules,
                                            const assets::VirtualFileSystem& files);

// This machine's identity for the versus protocol.
core::Result<game::versus::Identity> versusIdentity(std::string gameVersion,
                                                    const ruleset::Ruleset& rules,
                                                    const assets::VirtualFileSystem& files);

// A tick's input of one side: the command it gives (none: 0).
[[nodiscard]] std::uint32_t versusInputOf(game::battle::Command command);
[[nodiscard]] game::battle::Command versusCommandOf(std::uint32_t input);

// Gives a tick's two inputs to the battle, the host's (1P) first, then the guest's (2P). A command
// the battle refuses (not in that side's menu, the finisher not charged) is dropped the same way on
// both machines.
void commandVersus(game::battle::Battle& battle, const std::array<std::uint32_t, 2>& inputs);

// How a finished versus battle counts for the series (vs_combat.c: VS__combatMain): the host's
// fighter is the battle's partner, so its victory is the host's; a draw at the clock is lost by
// both.
[[nodiscard]] game::versus::RoundResult roundResultOf(const game::battle::BattleResult& result);

// The seed of a battle of the series: the agreed one plus the battle's number.
[[nodiscard]] std::uint64_t roundSeed(const game::versus::SeriesSetup& setup, std::int32_t round);

} // namespace dwe::session
