#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/partner/care_rules.hpp>
#include <dwe/game/save/sections.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/poop_adapter.hpp>

#include <string>
#include <utility>

#include "poop_messages.hpp"

namespace dwe::session {

namespace {

using assets::YamlReader;
using core::YamlNode;
using game::partner::EventKind;
using game::partner::PoopPile;
using game::partner::PoopPiles;
using game::partner::Tile;

constexpr std::string_view LookKey = "look";
constexpr std::string_view ModelKey = "model";
constexpr std::string_view ModelSizeKey = "model_size";
constexpr std::string_view WildKey = "wild";
constexpr std::string_view SquatClipKey = "squat_clip";
constexpr std::string_view AfterClipKey = "after_clip";
constexpr std::string_view EatKey = "eat";
constexpr std::string_view LargeFromKey = "large_from";
constexpr std::string_view SmallReachKey = "small_reach";
constexpr std::string_view LargeReachKey = "large_reach";
constexpr std::string_view EatClipKey = "eat_clip";
constexpr std::string_view CursorKey = "cursor";
constexpr std::string_view PilesKey = "piles";
constexpr std::string_view SlotKey = "slot";
constexpr std::string_view MapKey = "map";
constexpr std::string_view XKey = "x";
constexpr std::string_view YKey = "y";
constexpr std::string_view SizeKey = "size";

std::int32_t positive(YamlReader& reader, const YamlNode& root, std::string_view key) {
  const YamlNode& node = reader.field(root, key);
  const auto value = reader.integer<std::int32_t>(node);
  if (reader.ok() && value <= 0) {
    reader.fail(node, core::formatMessage(poop_messages::NotPositive, key));
  }
  return value;
}

YamlNode integerNode(std::int64_t value) {
  return YamlNode::scalar(std::to_string(value));
}

std::uint32_t clip(YamlReader& reader, const YamlNode& root, std::string_view key) {
  return reader.integer<std::uint32_t>(reader.field(root, key));
}

std::optional<WildPoopRules> wildRules(YamlReader& reader, const YamlNode& root) {
  const YamlNode* node = root.find(WildKey);
  if (node == nullptr || !reader.expectKeys(*node, WildKey, {SquatClipKey, AfterClipKey})) {
    return std::nullopt;
  }
  return WildPoopRules{.squatClip = clip(reader, *node, SquatClipKey),
                       .afterClip = clip(reader, *node, AfterClipKey)};
}

std::optional<EatPoopRules> eatRules(YamlReader& reader, const YamlNode& root) {
  const YamlNode* node = root.find(EatKey);
  if (node == nullptr ||
      !reader.expectKeys(*node, EatKey, {LargeFromKey, SmallReachKey, LargeReachKey, EatClipKey})) {
    return std::nullopt;
  }
  return EatPoopRules{.largeFrom = positive(reader, *node, LargeFromKey),
                      .smallReach = positive(reader, *node, SmallReachKey),
                      .largeReach = positive(reader, *node, LargeReachKey),
                      .eatClip = clip(reader, *node, EatClipKey)};
}

} // namespace

core::Result<PoopPileRules> readPoopPileRules(const ruleset::Section& section) {
  YamlReader reader;
  const YamlNode& root = section.node;
  if (!reader.expectKeys(root, poop_messages::SectionText, {LookKey}, {WildKey, EatKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  const YamlNode& look = reader.field(root, LookKey);
  if (!reader.expectKeys(look, LookKey, {ModelKey, ModelSizeKey})) {
    return std::unexpected{reader.error(section.file)};
  }
  const YamlNode& modelNode = reader.field(look, ModelKey);
  const std::string text = reader.text(modelNode);
  auto model = assets::AssetId::parse(text);
  if (!model) {
    reader.fail(modelNode,
                core::formatMessage(poop_messages::BadModel, text, model.error().message));
    return std::unexpected{reader.error(section.file)};
  }
  PoopPileRules rules{
      .look = {.model = std::move(*model), .modelSize = positive(reader, look, ModelSizeKey)},
      .wild = wildRules(reader, root),
      .eat = eatRules(reader, root)};
  return reader.finish(std::move(rules), section.file);
}

class PoopAdapter::Serializer final : public game::save::SectionSerializer {
public:
  explicit Serializer(PoopAdapter& adapter) : adapter_(&adapter) {}

  [[nodiscard]] core::Result<YamlNode> write() const override {
    YamlNode piles = YamlNode::sequence();
    std::size_t cursor = 0;
    if (adapter_->piles_) {
      const auto slots = adapter_->piles_->slots();
      for (std::size_t slot = 0; slot < slots.size(); ++slot) {
        const PoopPile& pile = slots[slot];
        if (pile.size == 0) {
          continue;
        }
        YamlNode entry = YamlNode::mapping();
        entry.set(std::string{SlotKey}, integerNode(static_cast<std::int64_t>(slot)));
        entry.set(std::string{MapKey}, YamlNode::scalar(pile.map));
        entry.set(std::string{XKey}, integerNode(pile.tile.x));
        entry.set(std::string{YKey}, integerNode(pile.tile.y));
        entry.set(std::string{SizeKey}, integerNode(pile.size));
        piles.append(std::move(entry));
      }
      cursor = adapter_->piles_->cursor();
    }
    YamlNode root = YamlNode::mapping();
    root.set(std::string{CursorKey}, integerNode(static_cast<std::int64_t>(cursor)));
    root.set(std::string{PilesKey}, std::move(piles));
    return root;
  }

  core::Result<void> read(const YamlNode& state) override {
    YamlReader reader;
    if (!reader.expectKeys(state, poop_messages::SaveText, {CursorKey, PilesKey})) {
      return std::unexpected{reader.error(poop_messages::SaveName)};
    }
    const auto cursor = reader.integer<std::uint32_t>(reader.field(state, CursorKey));
    std::vector<PoopPile> slots;
    for (const YamlNode& entry : reader.sequence(reader.field(state, PilesKey), PilesKey)) {
      if (!reader.expectKeys(entry, poop_messages::PileText,
                             {SlotKey, MapKey, XKey, YKey, SizeKey})) {
        break;
      }
      const auto slot = reader.integer<std::uint16_t>(reader.field(entry, SlotKey));
      PoopPile pile{.map = reader.text(reader.field(entry, MapKey)),
                    .tile = {.x = reader.integer<std::int32_t>(reader.field(entry, XKey)),
                             .y = reader.integer<std::int32_t>(reader.field(entry, YKey))},
                    .size = reader.integer<std::int32_t>(reader.field(entry, SizeKey))};
      if (slot >= slots.size()) {
        slots.resize(static_cast<std::size_t>(slot) + 1);
      }
      slots[slot] = std::move(pile);
    }
    auto read = reader.finish(std::move(slots), poop_messages::SaveName);
    if (!read) {
      return std::unexpected{read.error()};
    }
    adapter_->pending_.clear();
    if (adapter_->piles_) {
      adapter_->piles_->restore(std::move(*read), cursor);
    }
    return {};
  }

  core::Result<void> readMissing() override {
    core::logWarning("{}", poop_messages::MissingInSave);
    adapter_->pending_.clear();
    if (adapter_->piles_) {
      adapter_->piles_->restore({}, 0);
    }
    return {};
  }

private:
  PoopAdapter* adapter_;
};

PoopAdapter::PoopAdapter() : serializer_{std::make_unique<Serializer>(*this)} {}

PoopAdapter::~PoopAdapter() = default;

ruleset::SectionChecks PoopAdapter::sectionChecks() const {
  return {
      {std::string{PoopPilesSection}, [](const ruleset::Section& section) -> core::Result<void> {
         auto rules = readPoopPileRules(section);
         if (!rules) {
           return std::unexpected{rules.error()};
         }
         return {};
       }}};
}

core::Result<void> PoopAdapter::readRules(const ruleset::Ruleset& rules) {
  rules_.reset();
  if (const ruleset::Section* section = rules.section(PoopPilesSection); section != nullptr) {
    auto read = readPoopPileRules(*section);
    if (!read) {
      return std::unexpected{read.error()};
    }
    rules_ = std::move(*read);
  }
  // The limits of the piles are the care rules' (createPoopPile); the piles placed stay.
  const ruleset::Section* care = rules.section(game::partner::CareSection);
  core::Result<game::partner::CareRules> careRules =
      core::fail(std::string{game::partner::CareSection});
  if (care != nullptr) {
    careRules = game::partner::readCareRules(*care);
  }
  if (!careRules) {
    core::logWarning("{}", core::formatMessage(poop_messages::NoPiles, careRules.error().message));
    piles_.reset();
    return {};
  }
  PoopPiles piles{careRules->toilet};
  if (piles_) {
    const auto slots = piles_->slots();
    piles.restore({slots.begin(), slots.end()}, piles_->cursor());
  }
  piles_ = std::move(piles);
  return {};
}

core::Result<void> PoopAdapter::addSaveSections(game::save::SaveSections& sections) {
  return sections.add(std::string{poop_messages::SaveName}, *serializer_,
                      game::save::MigrationChain{});
}

core::Result<void> PoopAdapter::startNewGame(GameSession& /*session*/) {
  pending_.clear();
  if (piles_) {
    piles_->restore({}, 0);
  }
  return {};
}

void PoopAdapter::routePartnerEvent(GameSession& /*session*/,
                                    const game::partner::PartnerEvent& event) {
  if (event.kind == EventKind::PoopedOnGround) {
    pending_.push_back({.size = event.value});
  }
}

std::optional<PendingPile> PoopAdapter::nextPending() const {
  if (pending_.empty()) {
    return std::nullopt;
  }
  return pending_.front();
}

std::optional<Tile> PoopAdapter::placePending(GameSession& session, std::string_view map, Tile tile,
                                              std::int32_t rotation) {
  if (pending_.empty()) {
    return std::nullopt;
  }
  const PendingPile pile = pending_.front();
  pending_.pop_front();
  if (!piles_) {
    return std::nullopt;
  }
  const Tile placed = piles_->place(map, tile, rotation, pile.size, session.random());
  core::logDebug("[port:poop] createPoopPile (partner_impl.c): {} size {} at ({}, {}) from ({}, "
                 "{}) facing {}",
                 map, pile.size, placed.x, placed.y, tile.x, tile.y, rotation);
  return placed;
}

std::vector<PoopPile> PoopAdapter::onMap(std::string_view map) const {
  return piles_ ? piles_->onMap(map) : std::vector<PoopPile>{};
}

std::optional<std::int32_t> PoopAdapter::remove(std::string_view map, Tile tile) {
  return piles_ ? piles_->remove(map, tile) : std::nullopt;
}

} // namespace dwe::session
