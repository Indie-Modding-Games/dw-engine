#include <dwe/assets/yaml_reader.hpp>
#include <dwe/game/tournament/cup_extras.hpp>

#include <algorithm>
#include <cctype>
#include <string_view>

namespace dwe::game::tournament {

namespace {

using assets::YamlReader;
using core::YamlNode;

constexpr std::string_view BracketKey = "bracket";
constexpr std::string_view FramesPerSecondKey = "frames_per_second";
constexpr std::string_view SoundBankKey = "sound_bank";
constexpr std::string_view MusicKey = "music";
constexpr std::string_view FontKey = "font";
constexpr std::string_view IntroKey = "intro";
constexpr std::string_view MatchKey = "match";
constexpr std::string_view ChampionKey = "champion";
constexpr std::string_view ChampionFadeKey = "champion_fade";
constexpr std::string_view ChampionTailKey = "champion_tail";
constexpr std::string_view TexturesKey = "textures";
constexpr std::string_view BoardKey = "board";
constexpr std::string_view PagesKey = "pages";
constexpr std::string_view CardsKey = "cards";
constexpr std::string_view FlashKey = "flash";
constexpr std::string_view PageKey = "page";
constexpr std::string_view UKey = "u";
constexpr std::string_view VKey = "v";
constexpr std::string_view SizeKey = "size";
constexpr std::string_view PaletteKey = "palette";

constexpr std::string_view ExtraPrizesKey = "extra_prizes";
constexpr std::string_view CupKey = "cup";
constexpr std::string_view ItemKey = "item";
constexpr std::string_view AmountKey = "amount";
constexpr std::string_view MoneyKey = "money";

constexpr std::string_view MedalKey = "medal";
constexpr std::string_view CupGroupsKey = "cup_groups";
constexpr std::string_view WinsKey = "wins";
constexpr std::string_view BeatenTriggerKey = "beaten_trigger";
constexpr std::string_view LinesSectionKey = "lines_section";

constexpr std::string_view GradesKey = "grades";

std::int32_t integer(YamlReader& reader, const YamlNode& parent, std::string_view key) {
  return reader.integer<std::int32_t>(reader.field(parent, key));
}

std::string_view sectionName(const ruleset::Section& section) {
  return section.file;
}

} // namespace

core::Result<std::optional<BracketRules>> parseBracketRules(const ruleset::Section& section) {
  const YamlNode* node = section.node.isMapping() ? section.node.find(BracketKey) : nullptr;
  if (node == nullptr) {
    return std::optional<BracketRules>{};
  }
  YamlReader reader;
  BracketRules rules;
  if (reader.expectKeys(*node, BracketKey,
                        {FramesPerSecondKey, SoundBankKey, MusicKey, ChampionFadeKey,
                         ChampionTailKey, TexturesKey, FlashKey})) {
    rules.framesPerSecond = integer(reader, *node, FramesPerSecondKey);
    rules.soundBank = integer(reader, *node, SoundBankKey);
    const YamlNode& music = reader.field(*node, MusicKey);
    if (reader.expectKeys(music, MusicKey, {FontKey, IntroKey, MatchKey, ChampionKey})) {
      rules.musicFont = integer(reader, music, FontKey);
      rules.introTrack = integer(reader, music, IntroKey);
      rules.matchTrack = integer(reader, music, MatchKey);
      rules.championTrack = integer(reader, music, ChampionKey);
    }
    rules.championFadeFrames = integer(reader, *node, ChampionFadeKey);
    rules.championTailFrames = integer(reader, *node, ChampionTailKey);
    const YamlNode& textures = reader.field(*node, TexturesKey);
    if (reader.expectKeys(textures, TexturesKey, {BoardKey, PagesKey, CardsKey})) {
      rules.board = reader.text(reader.field(textures, BoardKey));
      const auto pages =
          reader.sequence(reader.field(textures, PagesKey), PagesKey, rules.pages.size());
      for (std::size_t page = 0; page < pages.size(); ++page) {
        rules.pages.at(page) = reader.text(pages[page]);
      }
      rules.cards = reader.text(reader.field(textures, CardsKey));
    }
    const YamlNode& flash = reader.field(*node, FlashKey);
    if (reader.expectKeys(flash, FlashKey, {PageKey, UKey, VKey, SizeKey, PaletteKey})) {
      const std::int32_t size = integer(reader, flash, SizeKey);
      rules.flash = {.page = integer(reader, flash, PageKey),
                     .u = integer(reader, flash, UKey),
                     .v = integer(reader, flash, VKey),
                     .width = size,
                     .height = size,
                     .palette = integer(reader, flash, PaletteKey)};
    }
    if (reader.ok() && rules.framesPerSecond <= 0) {
      reader.fail(reader.field(*node, FramesPerSecondKey), "the board needs a frame rate");
    }
  }
  if (!reader.ok()) {
    return std::unexpected{reader.error(sectionName(section))};
  }
  return std::optional<BracketRules>{std::move(rules)};
}

std::vector<std::string> parseStadiumGrades(const core::YamlNode& stadium) {
  std::vector<std::string> grades;
  const YamlNode* node = stadium.isMapping() ? stadium.find(GradesKey) : nullptr;
  if (node == nullptr) {
    return grades;
  }
  YamlReader reader;
  for (const YamlNode& grade : reader.sequence(*node, GradesKey)) {
    grades.push_back(reader.text(grade));
  }
  return reader.ok() ? grades : std::vector<std::string>{};
}

std::string bracketCardImage(const BracketRules& rules, std::span<const std::string> grades,
                             std::int32_t cup) {
  if (cup < 0 || std::cmp_greater_equal(cup, grades.size()) || rules.cards.empty()) {
    return {};
  }
  // loadTIMFile(STD_GRADES[cup]): the file's name without its folder and extension, lower case as
  // the extraction writes it.
  std::string_view path = grades[static_cast<std::size_t>(cup)];
  if (const auto slash = path.find_last_of("\\/"); slash != std::string_view::npos) {
    path.remove_prefix(slash + 1);
  }
  if (const auto dot = path.find('.'); dot != std::string_view::npos) {
    path = path.substr(0, dot);
  }
  if (path.empty()) {
    return {};
  }
  std::string name{path};
  std::ranges::transform(name, name.begin(), [](unsigned char letter) {
    return static_cast<char>(std::tolower(letter));
  });
  return rules.cards + "/" + name;
}

core::Result<std::vector<ExtraPrize>> parseExtraPrizes(const ruleset::Section& section) {
  std::vector<ExtraPrize> prizes;
  const YamlNode* node = section.node.isMapping() ? section.node.find(ExtraPrizesKey) : nullptr;
  if (node == nullptr) {
    return prizes;
  }
  YamlReader reader;
  for (const YamlNode& entry : reader.sequence(*node, ExtraPrizesKey)) {
    if (!reader.expectKeys(entry, ExtraPrizesKey, {CupKey}, {ItemKey, AmountKey, MoneyKey})) {
      break;
    }
    ExtraPrize prize{.cup = integer(reader, entry, CupKey),
                     .item = reader.optionalInteger<std::int32_t>(entry, ItemKey),
                     .amount = reader.optionalInteger<std::int32_t>(entry, AmountKey).value_or(1),
                     .money = reader.optionalInteger<std::int32_t>(entry, MoneyKey).value_or(0)};
    if (!prize.item && prize.money == 0) {
      reader.fail(entry, "a prize gives an item or money");
    }
    prizes.push_back(prize);
  }
  if (!reader.ok()) {
    return std::unexpected{reader.error(sectionName(section))};
  }
  return prizes;
}

std::vector<ExtraPrize> extraPrizesOf(std::span<const ExtraPrize> prizes, std::int32_t cup) {
  std::vector<ExtraPrize> found;
  std::ranges::copy_if(prizes, std::back_inserter(found),
                       [cup](const ExtraPrize& prize) { return prize.cup == cup; });
  return found;
}

core::Result<std::optional<TournamentMedalRules>>
parseTournamentMedalRules(const ruleset::Section& section) {
  const YamlNode* node = section.node.isMapping() ? section.node.find(MedalKey) : nullptr;
  if (node == nullptr) {
    return std::optional<TournamentMedalRules>{};
  }
  YamlReader reader;
  TournamentMedalRules rules;
  if (reader.expectKeys(*node, MedalKey,
                        {CupGroupsKey, WinsKey, CardsKey, BeatenTriggerKey, LinesSectionKey})) {
    for (const YamlNode& group : reader.sequence(reader.field(*node, CupGroupsKey), CupGroupsKey)) {
      const auto pair = reader.sequence(group, CupGroupsKey, 2);
      if (pair.size() == 2) {
        rules.cupGroups.emplace_back(reader.integer<std::int32_t>(pair[0]),
                                     reader.integer<std::int32_t>(pair[1]));
      }
    }
    rules.wins = integer(reader, *node, WinsKey);
    rules.cards = integer(reader, *node, CardsKey);
    rules.beatenTrigger = integer(reader, *node, BeatenTriggerKey);
    rules.linesSection = integer(reader, *node, LinesSectionKey);
  }
  if (!reader.ok()) {
    return std::unexpected{reader.error(sectionName(section))};
  }
  return std::optional<TournamentMedalRules>{std::move(rules)};
}

std::optional<std::int32_t> tournamentMedal(const TournamentMedalRules& rules,
                                            std::span<const std::int32_t> medalIds,
                                            const TournamentMedalFacts& facts) {
  const auto allWon = [&](const std::pair<std::int32_t, std::int32_t>& group) {
    for (std::int32_t i = 0; i < group.second; ++i) {
      if (!facts.trigger(group.first + i)) {
        return false;
      }
    }
    return true;
  };
  std::optional<std::int32_t> result;
  for (std::size_t group = 0; group < rules.cupGroups.size() && !result; ++group) {
    if (allWon(rules.cupGroups[group])) {
      result = static_cast<std::int32_t>(group);
    }
  }
  const auto groups = static_cast<std::int32_t>(rules.cupGroups.size());
  if (!result && facts.tournamentWins >= rules.wins) {
    result = groups;
  }
  if (!result) {
    for (std::int32_t card = 0; card < rules.cards; ++card) {
      if (facts.cardCopies(card) == 0) {
        return std::nullopt;
      }
    }
    if (!facts.trigger(rules.beatenTrigger)) {
      return std::nullopt;
    }
    result = groups + 1;
  }
  // hasMedal(TOURNAMENT_MEDAL_IDS[result]): the first condition met decides, held or not.
  if (std::cmp_greater_equal(*result, medalIds.size()) ||
      facts.hasMedal(medalIds[static_cast<std::size_t>(*result)])) {
    return std::nullopt;
  }
  return result;
}

} // namespace dwe::game::tournament
