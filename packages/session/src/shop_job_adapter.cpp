#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/log.hpp>
#include <dwe/session/game_session.hpp>
#include <dwe/session/inventory_adapter.hpp>
#include <dwe/session/messages.hpp>
#include <dwe/session/shop_job_adapter.hpp>
#include <dwe/session/story_adapter.hpp>

#include <format>
#include <string>
#include <utility>

namespace dwe::session {

namespace {

using assets::YamlReader;
using core::YamlNode;
namespace shop_job = game::shop_job;

constexpr std::string_view JobsKey = "jobs";
constexpr std::string_view RunningKey = "running";
// The original's price levels are 1 to 9 (pstat 117).
constexpr std::int32_t FirstStoryLevel = 1;
constexpr std::int32_t BonusCount = 1;

ShopJobTurn turnOf(const shop_job::OfferResult& result) {
  if (result.sold) {
    return ShopJobTurn::Sold;
  }
  return result.leaves ? ShopJobTurn::Left : ShopJobTurn::Refused;
}

} // namespace

class ShopJobAdapter::Serializer final : public game::save::SectionSerializer {
public:
  explicit Serializer(ShopJobAdapter& job) : job_(&job) {}

  [[nodiscard]] core::Result<YamlNode> write() const override {
    YamlNode root = YamlNode::mapping();
    root.set(std::string{JobsKey}, YamlNode::scalar(std::format("{}", job_->jobs_)));
    root.set(std::string{RunningKey}, YamlNode::scalar(job_->running_ ? "true" : "false"));
    return root;
  }

  core::Result<void> read(const YamlNode& state) override {
    YamlReader reader;
    std::uint64_t jobs = 0;
    bool running = false;
    if (reader.expectKeys(state, SaveName, {JobsKey, RunningKey})) {
      jobs = reader.integer<std::uint64_t>(reader.field(state, JobsKey));
      running = reader.boolean(reader.field(state, RunningKey));
    }
    if (auto done = reader.finish(true, std::string{SaveName}); !done) {
      return std::unexpected{done.error()};
    }
    job_->jobs_ = jobs;
    job_->running_ = running;
    return {};
  }

  // A save from before the improved job: no job has been played.
  core::Result<void> readMissing() override {
    job_->jobs_ = 0;
    job_->running_ = false;
    return {};
  }

private:
  ShopJobAdapter* job_;
};

ShopJobAdapter::ShopJobAdapter(ShopJobAdapterDesc desc)
    : desc_{desc}, serializer_(std::make_unique<Serializer>(*this)) {}

ShopJobAdapter::~ShopJobAdapter() = default;

core::Result<void> ShopJobAdapter::readRules(const ruleset::Ruleset& rules) {
  rules_.reset();
  haggle_.reset();
  const ruleset::Section* section = rules.section(shop_job::ShopJobSectionName);
  if (section == nullptr) {
    return {};
  }
  auto read = shop_job::parseShopJobRules(*section);
  if (!read) {
    return std::unexpected{read.error()};
  }
  rules_ = std::move(*read);
  return {};
}

core::Result<void> ShopJobAdapter::addSaveSections(game::save::SaveSections& sections) {
  return sections.add(std::string{SaveName}, *serializer_, game::save::MigrationChain{});
}

core::Result<void> ShopJobAdapter::startNewGame(GameSession& /*session*/) {
  jobs_ = 0;
  running_ = false;
  lineup_.reset();
  haggle_.reset();
  turn_.reset();
  return {};
}

void ShopJobAdapter::afterLoad(GameSession& session) {
  lineup_.reset();
  haggle_.reset();
  turn_.reset();
  // The running job's line comes back from its number: the same customers as before the save.
  if (running_ && jobs_ > 0 && rules_) {
    makeLine(session.seed());
  }
}

void ShopJobAdapter::makeLine(std::uint64_t gameSeed) {
  ruleset::Random random{shop_job::lineupSeed(gameSeed, jobs_)};
  lineup_ = shop_job::makeLineup(*rules_, random);
  lineBegun_ = false;
  core::logInfo("{}", core::formatMessage(messages::ShopJobLine, jobs_, lineup_->perfectScore));
}

void ShopJobAdapter::startJob(std::uint64_t gameSeed) {
  haggle_.reset();
  turn_.reset();
  if (!rules_) {
    return;
  }
  ++jobs_;
  running_ = true;
  makeLine(gameSeed);
}

const shop_job::Customer* ShopJobAdapter::customerAt(std::size_t index) const {
  if (!lineup_ || index >= lineup_->customers.size()) {
    return nullptr;
  }
  return &lineup_->customers[index];
}

bool ShopJobAdapter::beginCustomer(std::size_t index) {
  const shop_job::Customer* customer = customerAt(index);
  turn_.reset();
  if (customer == nullptr || !rules_) {
    haggle_.reset();
    return false;
  }
  customerIndex_ = index;
  lineBegun_ = true;
  haggle_.emplace(*rules_, *customer);
  return true;
}

bool ShopJobAdapter::play(ShopJobAction action, shop_job::Level level) {
  if (!haggle_ || haggle_->over() || turn_) {
    return false;
  }
  switch (action) {
  case ShopJobAction::Offer: {
    if (level < 0 || level >= shop_job::NoSale) {
      return false;
    }
    const shop_job::OfferResult result = haggle_->offer(level);
    turn_ = Turn{.result = turnOf(result),
                 .level = level,
                 .face = rules_->rulesOf(result.reaction).face,
                 .profit = result.profit};
    return true;
  }
  case ShopJobAction::Talk:
    if (!haggle_->talk()) {
      return false;
    }
    turn_ = Turn{.result = ShopJobTurn::Talked, .level = 0, .face = 0, .profit = 0};
    return true;
  case ShopJobAction::Dismiss:
    haggle_->dismiss();
    turn_ = Turn{.result = ShopJobTurn::Dismissed, .level = 0, .face = 0, .profit = 0};
    return true;
  }
  return false;
}

core::Result<void> ShopJobAdapter::finishTurn() {
  if (!rules_ || desc_.story == nullptr) {
    return core::fail(std::string{messages::NoShopJob});
  }
  // A screen closed without a turn (the app shut it) sends the customer away.
  const Turn turn = turn_.value_or(Turn{});
  turn_.reset();
  game::story::StoryState& state = desc_.story->state();
  for (const auto& [variable, value] :
       {std::pair{rules_->resultVariable, static_cast<std::int32_t>(turn.result)},
        std::pair{rules_->levelVariable, turn.level + FirstStoryLevel},
        std::pair{rules_->faceVariable, turn.face}}) {
    if (auto written = state.setVariable(variable, value); !written) {
      return std::unexpected{written.error()};
    }
  }
  if (turn.profit != 0) {
    desc_.story->setShopTally(desc_.story->shopTally() + turn.profit);
  }
  core::logDebug("{}", core::formatMessage(messages::ShopJobTurnLog, customerIndex_,
                                           static_cast<int>(turn.result), turn.level, turn.profit,
                                           desc_.story->shopTally()));
  if (turn.result != ShopJobTurn::Refused && turn.result != ShopJobTurn::Talked) {
    haggle_.reset();
  }
  return {};
}

core::Result<ShopJobAdapter::Bonus> ShopJobAdapter::payBonus() {
  running_ = false;
  lineup_.reset();
  haggle_.reset();
  if (!rules_ || desc_.story == nullptr || desc_.inventory == nullptr) {
    return core::fail(std::string{messages::NoShopJob});
  }
  if (desc_.story->shopTally() < rules_->bonusFrom) {
    return Bonus{};
  }
  const std::optional<assets::ItemHandle> item =
      desc_.inventory->items().handleOf(rules_->bonusItem);
  if (!item) {
    return core::fail(core::formatMessage(messages::ShopJobNoBonusItem, rules_->bonusItem));
  }
  const bool given = desc_.inventory->inventory().add(*item, BonusCount).taken();
  core::logInfo("{}", core::formatMessage(messages::ShopJobBonus, rules_->bonusItem, given));
  return Bonus{.earned = true, .given = given};
}

} // namespace dwe::session
