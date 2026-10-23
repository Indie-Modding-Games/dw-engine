#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/log.hpp>
#include <dwe/core/message.hpp>
#include <dwe/core/yaml.hpp>
#include <dwe/game/save/sections.hpp>
#include <dwe/session/cards_adapter.hpp>
#include <dwe/session/story_adapter.hpp>

#include <string>
#include <utility>

#include "cards_messages.hpp"

namespace dwe::session {

namespace {

using core::YamlNode;
using game::inventory::CardCollection;
using game::inventory::CardState;

// showCardTextbox: PSTAT_249 holds the card's Digimon for the pack's line.
constexpr std::int32_t PackDigimonVariable = 249;

} // namespace

class CardsAdapter::Serializer final : public game::save::SectionSerializer {
public:
  explicit Serializer(CardsAdapter& adapter) : adapter_(&adapter) {}

  [[nodiscard]] core::Result<YamlNode> write() const override {
    YamlNode copies = YamlNode::sequence();
    if (adapter_->collection_) {
      for (const std::int32_t count : adapter_->collection_->state().copies) {
        copies.append(YamlNode::scalar(std::to_string(count)));
      }
    }
    YamlNode root = YamlNode::mapping();
    root.set(std::string{cards_messages::CopiesKey}, std::move(copies));
    return root;
  }

  core::Result<void> read(const YamlNode& state) override {
    assets::YamlReader reader;
    if (!reader.expectKeys(state, cards_messages::SaveText, {cards_messages::CopiesKey})) {
      return std::unexpected{reader.error(cards_messages::SaveName)};
    }
    CardState cards;
    for (const YamlNode& count : reader.sequence(reader.field(state, cards_messages::CopiesKey),
                                                 cards_messages::CopiesKey)) {
      cards.copies.push_back(reader.integer<std::int32_t>(count));
    }
    auto read = reader.finish(std::move(cards), cards_messages::SaveName);
    if (!read) {
      return std::unexpected{read.error()};
    }
    if (adapter_->collection_) {
      adapter_->collection_->restore(std::move(*read));
    }
    return {};
  }

  core::Result<void> readMissing() override {
    core::logWarning("{}", cards_messages::MissingInSave);
    if (adapter_->collection_) {
      adapter_->collection_->restore({});
    }
    return {};
  }

private:
  CardsAdapter* adapter_;
};

CardsAdapter::CardsAdapter(StoryAdapter& story, std::optional<assets::CardTable> table,
                           const assets::DigimonTable* digimon)
    : story_{&story}, table_{std::move(table)}, serializer_{std::make_unique<Serializer>(*this)} {
  if (!table_) {
    return;
  }
  for (const assets::CardEntry& card : table_->cards) {
    if (card.index < 0) {
      continue;
    }
    if (std::cmp_greater_equal(card.index, digimonNumbers_.size())) {
      digimonNumbers_.resize(static_cast<std::size_t>(card.index) + 1);
    }
    const assets::DigimonRow* row = digimon != nullptr ? digimon->find(card.digimon) : nullptr;
    if (row != nullptr) {
      digimonNumbers_[static_cast<std::size_t>(card.index)] = row->index;
    }
  }
}

CardsAdapter::~CardsAdapter() = default;

ruleset::SectionChecks CardsAdapter::sectionChecks() const {
  return {{std::string{game::inventory::CardsSectionName},
           [](const ruleset::Section& section) -> core::Result<void> {
             auto rules = game::inventory::parseCardRules(section);
             if (!rules) {
               return std::unexpected{rules.error()};
             }
             return {};
           }}};
}

core::Result<void> CardsAdapter::readRules(const ruleset::Ruleset& rules) {
  const ruleset::Section* section = rules.section(game::inventory::CardsSectionName);
  if (section == nullptr || !table_) {
    core::logDebug("{}", cards_messages::NoCards);
    collection_.reset();
    return {};
  }
  auto read = game::inventory::parseCardRules(*section);
  if (!read) {
    return std::unexpected{read.error()};
  }
  if (collection_) {
    collection_->setRules(std::move(*read));
  } else {
    collection_.emplace(std::move(*read), *table_);
  }
  return {};
}

core::Result<void> CardsAdapter::addSaveSections(game::save::SaveSections& sections) {
  return sections.add(std::string{cards_messages::SaveName}, *serializer_,
                      game::save::MigrationChain{});
}

core::Result<void> CardsAdapter::startNewGame(GameSession& /*session*/) {
  if (collection_) {
    collection_->restore({});
  }
  return {};
}

std::int32_t CardsAdapter::packDraws() const {
  return collection_ ? collection_->rules().pack.draws : 0;
}

std::optional<PackCard> CardsAdapter::drawPackCard(ruleset::Random& random) {
  if (!collection_) {
    return std::nullopt;
  }
  const std::optional<game::inventory::DrawnCard> drawn = collection_->drawPackCard(random);
  if (!drawn) {
    return std::nullopt;
  }
  const auto card = static_cast<std::size_t>(drawn->card);
  const std::optional<std::int32_t> digimon =
      card < digimonNumbers_.size() ? digimonNumbers_[card] : std::nullopt;
  if (digimon) {
    if (auto set =
            story_->state().setVariable(PackDigimonVariable, static_cast<std::uint8_t>(*digimon));
        !set) {
      core::logWarning("{}", core::formatMessage(cards_messages::DigimonNotSet, PackDigimonVariable,
                                                 drawn->card, set.error().message));
    }
  }
  return PackCard{.card = drawn->card, .line = drawn->line};
}

namespace {

core::Result<void> changeCopy(game::inventory::CardCollection* collection, std::int32_t card,
                              bool gives) {
  if (collection == nullptr) {
    core::logDebug("[port:cards] {} card {}: no cards, nothing", gives ? "give_card" : "take_card",
                   card);
    return {};
  }
  if (!(gives ? collection->giveCopy(card) : collection->takeCopy(card))) {
    return core::fail(core::formatMessage(cards_messages::NoSuchCard, card));
  }
  return {};
}

} // namespace

core::Result<void> CardsAdapter::giveCopy(std::int32_t card) {
  return changeCopy(collection_ ? &*collection_ : nullptr, card, true);
}

core::Result<void> CardsAdapter::takeCopy(std::int32_t card) {
  return changeCopy(collection_ ? &*collection_ : nullptr, card, false);
}

std::int32_t CardsAdapter::copies(std::int32_t card) const {
  return collection_ ? collection_->copies(card) : 0;
}

std::vector<game::inventory::HeldCard> CardsAdapter::heldCards() const {
  return collection_ ? collection_->heldCards() : std::vector<game::inventory::HeldCard>{};
}

} // namespace dwe::session
