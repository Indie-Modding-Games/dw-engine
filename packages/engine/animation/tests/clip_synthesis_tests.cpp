#include <dwe/animation/animation_player.hpp>
#include <dwe/animation/clip_sampling.hpp>
#include <dwe/animation/clip_synthesis.hpp>
#include <dwe/assets/character_model.hpp>
#include <dwe/assets/file_system.hpp>
#include <dwe/assets/pak.hpp>
#include <dwe/assets/partner_clips.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/test_support/user_assets.hpp>

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <format>
#include <map>
#include <optional>
#include <string>
#include <vector>

using dwe::animation::addPartnerClips;
using dwe::animation::sampleClip;
using dwe::assets::AnimationClip;
using dwe::assets::ChannelPath;
using dwe::assets::ModelData;
using dwe::assets::MotionKind;
using dwe::assets::NodeTransform;
using dwe::assets::parsePartnerClipBook;
using dwe::assets::PartnerClipBook;
using dwe::assets::PartnerClipMode;
using dwe::assets::PartnerClipRecipe;

namespace {

constexpr float TicksPerSecond = 20.0F;
constexpr std::uint32_t Ticks = 5;

glm::quat about(glm::vec3 axis, float degrees) {
  return glm::angleAxis(glm::radians(degrees), axis);
}

glm::vec4 asValue(glm::quat q) {
  return {q.x, q.y, q.z, q.w};
}

// Made-up skeletons: a root, hips `hips` units up and an arm hanging from the hips. The donor's
// arm has no rest turn; the other model's arm is turned a quarter about y in its rest pose, with
// its hips twice as high.
ModelData skeleton(float hips, glm::quat armRest) {
  ModelData model;
  model.nodes = {
      {.name = "bone_00", .parent = std::nullopt, .rest = {}},
      {.name = "bone_01", .parent = 0U, .rest = {.translation = {0, hips, 0}}},
      {.name = "bone_02", .parent = 1U, .rest = {.translation = {10, 0, 0}, .rotation = armRest}}};
  model.nodeOrder = {0, 1, 2};
  model.skin = dwe::assets::ModelSkin{
      .joints = {0, 1, 2},
      .inverseBindMatrices = {glm::mat4{1.0F}, glm::mat4{1.0F}, glm::mat4{1.0F}}};
  return model;
}

std::vector<float> tickTimes() {
  std::vector<float> times;
  times.reserve(Ticks);
  for (std::uint32_t t = 0; t < Ticks; ++t) {
    times.push_back(static_cast<float>(t) / TicksPerSecond);
  }
  return times;
}

// A clip like an extracted one: the root steps 10 forward a tick, the hips bob 4 up at tick 2 and
// the arm swings to 80 degrees about x; a sound of the species bank and one of another.
AnimationClip donorClip(std::uint32_t id) {
  AnimationClip clip;
  clip.name = std::format("anim_{:02}", id);
  clip.id = id;
  clip.ticksPerSecond = 20;
  clip.tickCount = Ticks;
  clip.frameCount = Ticks;
  clip.duration = static_cast<float>(Ticks - 1) / TicksPerSecond;
  clip.rootMotionNode = 0;
  clip.loops = {{.firstTick = 1, .lastTick = 4, .count = std::nullopt}};
  clip.endsInLoop = true;
  clip.repeat = dwe::assets::ClipRepeat{
      .start = 1 / TicksPerSecond, .end = 4 / TicksPerSecond, .gap = 1 / TicksPerSecond};
  clip.sounds = {{.tick = 1, .bank = 4, .sound = 2}, {.tick = 2, .bank = 1, .sound = 3}};
  const std::vector<float> times = tickTimes();
  std::vector<glm::vec4> root;
  std::vector<glm::vec4> hips;
  std::vector<glm::vec4> arm;
  for (std::uint32_t t = 0; t < Ticks; ++t) {
    root.emplace_back(0, 0, 10 * t, 0);
    hips.emplace_back(0, t == 2 ? 104.0F : 100.0F, 0, 0);
    arm.push_back(asValue(about({1, 0, 0}, 20.0F * static_cast<float>(t))));
  }
  clip.channels.push_back(
      {.node = 0, .path = ChannelPath::Translation, .times = times, .values = root});
  clip.channels.push_back(
      {.node = 1, .path = ChannelPath::Translation, .times = times, .values = hips});
  clip.channels.push_back(
      {.node = 2, .path = ChannelPath::Rotation, .times = times, .values = arm});
  return clip;
}

ModelData donorModel() {
  ModelData donor = skeleton(100.0F, glm::quat{1, 0, 0, 0});
  for (const std::uint32_t id : {0U, 2U, 40U, 46U, 47U}) {
    donor.clips.push_back(donorClip(id));
  }
  return donor;
}

ModelData targetModel() {
  ModelData target = skeleton(200.0F, about({0, 1, 0}, 90.0F));
  // Its own idle and a clip 40 of its scenes.
  AnimationClip idle = donorClip(0);
  idle.name = "own idle";
  target.clips.push_back(idle);
  AnimationClip scene = donorClip(40);
  scene.name = "own scene";
  target.clips.push_back(scene);
  return target;
}

PartnerClipBook bookOf(std::vector<std::uint32_t> clips) {
  PartnerClipBook book;
  book.clips = std::move(clips);
  book.firstTechnique = 46;
  book.technique = {.kind = MotionKind::Lunge, .angle = 30.0F, .height = 0.2F, .strike = 0.5F};
  return book;
}

std::vector<NodeTransform> poseAt(const ModelData& model, const AnimationClip& clip, float time) {
  std::vector<NodeTransform> pose(model.nodes.size());
  sampleClip(model, clip, time, pose);
  return pose;
}

// The arm's model-space rotation in a pose.
glm::quat armGlobal(const std::vector<NodeTransform>& pose) {
  return pose[0].rotation * pose[1].rotation * pose[2].rotation;
}

bool same(glm::quat a, glm::quat b) {
  return std::abs(std::abs(glm::dot(a, b)) - 1.0F) < 1.0e-4F;
}

} // namespace

TEST_CASE("a retargeted clip turns each joint as the donor's turns from its rest, in model space") {
  const ModelData donor = donorModel();
  ModelData target = targetModel();
  const PartnerClipRecipe recipe{
      .donor = "made_up", .mode = PartnerClipMode::Retarget, .bones = {{1, 1}, {2, 2}}};
  const auto made = addPartnerClips(target, donor, bookOf({0, 2, 40}), recipe);
  REQUIRE_MESSAGE(made.has_value(), (made ? "" : made.error().message));
  CHECK(made->made == std::vector<std::uint32_t>{0, 2, 40, 46, 47});
  CHECK(made->unmade.empty());

  const AnimationClip* walk = target.findClip(std::uint32_t{2});
  REQUIRE(walk != nullptr);
  CHECK(walk->tickCount == Ticks);
  CHECK(walk->ticksPerSecond == 20);
  REQUIRE(walk->repeat.has_value());
  CHECK(walk->repeat.value_or(dwe::assets::ClipRepeat{}).start ==
        doctest::Approx(1 / TicksPerSecond));
  // The donor's voice is not its own; the other sound stays.
  REQUIRE(walk->sounds.size() == 1);
  CHECK(walk->sounds.front().bank == 1);
  // The root moves as the donor's does.
  CHECK(dwe::animation::rootMotion(target, *walk, 4 / TicksPerSecond).z == doctest::Approx(40.0F));

  const glm::quat restArm = about({0, 1, 0}, 90.0F);
  for (std::uint32_t t = 0; t < Ticks; ++t) {
    CAPTURE(t);
    const float time = static_cast<float>(t) / TicksPerSecond;
    const std::vector<NodeTransform> pose = poseAt(target, *walk, time);
    // Turned 20 degrees a tick about the model's x, on top of its own rest turn.
    CHECK(same(armGlobal(pose), about({1, 0, 0}, 20.0F * static_cast<float>(t)) * restArm));
    // The hips bob twice as much: they stand twice as high.
    CHECK(pose[1].translation.y == doctest::Approx(t == 2 ? 208.0F : 200.0F));
  }
}

TEST_CASE("the made clips replace the model's own but those it keeps") {
  const ModelData donor = donorModel();
  ModelData target = targetModel();
  const PartnerClipRecipe recipe{
      .donor = "made_up", .mode = PartnerClipMode::Retarget, .sameBones = true, .keep = {0}};
  const auto made = addPartnerClips(target, donor, bookOf({0, 2, 3, 40}), recipe);
  REQUIRE(made.has_value());
  // 0 is kept; the donor has no 3.
  CHECK(made->made == std::vector<std::uint32_t>{2, 40, 46, 47});
  CHECK(made->unmade == std::vector<std::uint32_t>{3});
  CHECK(target.findClip(std::uint32_t{0})->name == "own idle");
  CHECK(target.findClip(std::uint32_t{40})->name != "own scene");
  CHECK(std::ranges::count_if(target.clips,
                              [](const AnimationClip& clip) { return clip.id == 40U; }) == 1);
  // The same bones: every joint of the effects is its own.
  CHECK(made->effectJoints.empty());
}

TEST_CASE("the effects' donor joints go on the joint the map ties to them or to an ancestor") {
  const ModelData donor = donorModel();
  ModelData target = targetModel();
  const PartnerClipRecipe recipe{
      .donor = "made_up", .mode = PartnerClipMode::Retarget, .bones = {{2, 1}}};
  const auto made = addPartnerClips(target, donor, bookOf({}), recipe);
  REQUIRE(made.has_value());
  // Donor joint 1 is this model's 2; joint 2, unmapped, takes its parent's; joint 0 the root.
  CHECK(made->effectJoints == std::map<std::int32_t, std::int32_t>{{1, 2}});
}

TEST_CASE("a procedural clip takes the donor's timing and lunges forward on its strike") {
  const ModelData donor = donorModel();
  ModelData target = targetModel();
  PartnerClipRecipe recipe{.donor = "made_up",
                           .mode = PartnerClipMode::Procedural,
                           .bones = {{1, 1}},
                           .groups = dwe::assets::BoneGroups{.body = 1, .rightArm = {2}}};
  const auto made = addPartnerClips(target, donor, bookOf({}), recipe);
  REQUIRE_MESSAGE(made.has_value(), (made ? "" : made.error().message));
  CHECK(made->made == std::vector<std::uint32_t>{46, 47});
  const AnimationClip* lunge = target.findClip(std::uint32_t{46});
  REQUIRE(lunge != nullptr);
  CHECK(lunge->tickCount == Ticks);
  CHECK(dwe::animation::rootMotion(target, *lunge, 4 / TicksPerSecond).z == doctest::Approx(40.0F));
  // At the strike (tick 2 of 0..4) the body leans 30 degrees forward and the hips move forward.
  const std::vector<NodeTransform> strike = poseAt(target, *lunge, 2 / TicksPerSecond);
  const glm::vec3 up = strike[1].rotation * glm::vec3{0, 1, 0};
  CHECK(glm::degrees(std::atan2(up.z, up.y)) == doctest::Approx(30.0F).epsilon(1e-3));
  CHECK(strike[1].translation.z == doctest::Approx(0.2F * 200.0F).epsilon(1e-3));
  // Back where it started at the end.
  const std::vector<NodeTransform> end = poseAt(target, *lunge, 4 / TicksPerSecond);
  CHECK(same(end[1].rotation, glm::quat{1, 0, 0, 0}));
}

TEST_CASE("an upright body moves forward where it would lean") {
  const ModelData donor = donorModel();
  ModelData target = targetModel();
  PartnerClipRecipe recipe{.donor = "made_up",
                           .mode = PartnerClipMode::Procedural,
                           .bones = {{1, 1}},
                           .groups = dwe::assets::BoneGroups{.body = 1, .upright = true}};
  REQUIRE(addPartnerClips(target, donor, bookOf({}), recipe).has_value());
  const AnimationClip* lunge = target.findClip(std::uint32_t{46});
  REQUIRE(lunge != nullptr);
  // At the strike the body stays level and its 30 degrees of lean move it forward instead: the
  // lunge's 0.2 of the hips' height and sin 30 of it more.
  const std::vector<NodeTransform> strike = poseAt(target, *lunge, 2 / TicksPerSecond);
  CHECK(same(strike[1].rotation, glm::quat{1, 0, 0, 0}));
  CHECK(strike[1].translation.z == doctest::Approx((0.2F + 0.5F) * 200.0F).epsilon(1e-3));
}

TEST_CASE("a recipe naming a joint the skin lacks changes nothing") {
  const ModelData donor = donorModel();
  ModelData target = targetModel();
  const std::size_t before = target.clips.size();
  const PartnerClipRecipe recipe{
      .donor = "made_up", .mode = PartnerClipMode::Retarget, .bones = {{1, 7}}};
  const auto made = addPartnerClips(target, donor, bookOf({2}), recipe);
  REQUIRE_FALSE(made.has_value());
  CHECK(made.error().message.find("no joint 7") != std::string::npos);
  CHECK(target.clips.size() == before);
}

TEST_CASE("the partner clip book reads its motions and recipes, and names its problems") {
  const auto book = parsePartnerClipBook(R"(
clips: [0, 2]
first_technique: 46
motions:
  0: {kind: breathe, height: 0.02}
technique: {kind: lunge, angle: 25, strike: 0.45}
species:
  aaaa: {donor: bbbb, bones: same, keep: [0]}
  cccc:
    donor: bbbb
    mode: procedural
    bones: {1: 1}
    groups: {body: 1, upright: true, spin: {joints: [2], axis: z, turns_per_second: 0.5}}
    motions: {2: {kind: stride, beats: 2, angle: 30}}
)");
  REQUIRE_MESSAGE(book.has_value(), (book ? "" : book.error().message));
  CHECK(book->clips == std::vector<std::uint32_t>{0, 2});
  const PartnerClipRecipe* same = book->find("aaaa");
  REQUIRE(same != nullptr);
  CHECK(same->sameBones);
  CHECK(same->donorJoint(5) == 5U);
  const PartnerClipRecipe* made = book->find("cccc");
  REQUIRE(made != nullptr);
  CHECK(made->mode == PartnerClipMode::Procedural);
  CHECK(made->donorJoint(1) == 1U);
  CHECK_FALSE(made->donorJoint(2).has_value());
  CHECK(made->groups.transform(
            [](const dwe::assets::BoneGroups& groups) { return groups.upright; }) == true);
  const auto spin =
      made->groups.and_then([](const dwe::assets::BoneGroups& groups) { return groups.spin; });
  CHECK(spin.transform([](const dwe::assets::SpinBones& bones) { return bones.axis; }) ==
        dwe::assets::SpinAxis::Z);
  const auto kindOf = [&](std::uint32_t clip) {
    return book->motionOf(*made, clip).transform([](const dwe::assets::MotionRecipe& motion) {
      return motion.kind;
    });
  };
  CHECK(kindOf(2) == MotionKind::Stride);
  CHECK(kindOf(0) == MotionKind::Breathe);
  CHECK(kindOf(50) == MotionKind::Lunge);
  CHECK_FALSE(book->motionOf(*made, 5).has_value());

  const auto wrongKind = parsePartnerClipBook(R"(
clips: []
first_technique: 46
motions: {0: {kind: dance}}
technique: {kind: lunge}
species: {}
)");
  REQUIRE_FALSE(wrongKind.has_value());
  CHECK(wrongKind.error().message.find("'dance' is not a motion kind") != std::string::npos);
  const auto noGroups = parsePartnerClipBook(R"(
clips: []
first_technique: 46
motions: {}
technique: {kind: lunge}
species: {aaaa: {donor: bbbb, mode: procedural, bones: {}}}
)");
  REQUIRE_FALSE(noGroups.has_value());
  CHECK(noGroups.error().message.find("needs its bone groups") != std::string::npos);
}

TEST_CASE("the game pack's partner clip book reads") {
  const dwe::assets::DiskFileSystem disk;
  const auto bytes =
      disk.readFile(std::filesystem::path{DWE_GAME_PACK_DIR} / "animations" / "partner_clips.yaml");
  REQUIRE(bytes.has_value());
  const auto book =
      parsePartnerClipBook({reinterpret_cast<const char*>(bytes->data()), bytes->size()});
  REQUIRE_MESSAGE(book.has_value(), (book ? "" : book.error().message));
  CHECK(book->species.size() == 11);
  for (const std::uint32_t clip : book->clips) {
    CAPTURE(clip);
    CHECK(book->motions.contains(clip));
  }
}

// Runs only when DWE_CONTENT names a content folder (scripts/dev.sh mounts it). Every species of
// the game pack's book gets, from the user's extracted models, every clip the partner's field and
// battle code plays that its donor has, and its battle and technique clips last and move as the
// donor's, so the battle's frames (steps, hits, effect spawns) line up with the donor's.
TEST_CASE("the user's disc, extracted: the partner clip book gives each species every clip") {
  const std::optional<std::filesystem::path> content = dwe::test_support::userContent();
  if (!content) {
    MESSAGE("DWE_CONTENT is not set: skipping the test on the extracted content");
    return;
  }
  const dwe::assets::DiskFileSystem disk;
  auto packs = dwe::assets::findPacks(disk, *content);
  REQUIRE(packs.has_value());
  packs->packs.push_back({.path = DWE_GAME_PACK_DIR, .kind = dwe::assets::FoundPack::Kind::Folder});
  const auto opened = dwe::assets::openPacks(disk, packs->packs, dwe::assets::PackOrigin::Engine);
  REQUIRE_MESSAGE(opened.has_value(), (opened ? "" : opened.error().message));
  const auto vfs = dwe::assets::VirtualFileSystem::mount(opened->locations);
  REQUIRE_MESSAGE(vfs.has_value(), (vfs ? "" : vfs.error().message));

  const auto book = dwe::assets::loadPartnerClipBook(
      *vfs, *dwe::assets::AssetId::parse(dwe::assets::PartnerClipBookAsset));
  REQUIRE_MESSAGE(book.has_value(), (book ? "" : book.error().message));
  const auto partner =
      dwe::assets::loadCharacterModel(*vfs, *dwe::assets::AssetId::parse("models/partner"));
  REQUIRE(partner.has_value());
  // The species' models sit beside the starting partner's, one folder each.
  const std::filesystem::path speciesFolder =
      std::filesystem::path{partner->model.str()}.parent_path().parent_path();
  const auto modelOf = [&](const std::string& species) {
    const auto id =
        dwe::assets::AssetId::parse((speciesFolder / species / "model").generic_string());
    REQUIRE(id.has_value());
    return dwe::assets::loadModel(*vfs, *id);
  };

  for (const auto& [species, recipe] : book->species) {
    CAPTURE(species);
    auto model = modelOf(species);
    const auto donor = modelOf(recipe.donor);
    REQUIRE(model.has_value());
    REQUIRE(donor.has_value());
    const auto made = addPartnerClips(*model, *donor, *book, recipe);
    REQUIRE_MESSAGE(made.has_value(), (made ? "" : made.error().message));
    // Only a clip the donor lacks too is left out (28, the rebirth, which some partner forms of
    // the original lack): the field stands it in as it does for the donor. Every clip of the
    // partner's animations is there.
    for (const std::uint32_t id : made->unmade) {
      CAPTURE(id);
      CHECK(donor->findClip(id) == nullptr);
    }
    std::vector<std::uint32_t> wanted;
    for (const std::uint32_t id : book->clips) {
      if (std::ranges::find(made->unmade, id) == made->unmade.end()) {
        wanted.push_back(id);
      }
    }
    for (const auto& [animation, clip] : partner->animations) {
      wanted.push_back(clip);
    }
    for (const AnimationClip& clip : donor->clips) {
      if (clip.id && *clip.id >= book->firstTechnique) {
        wanted.push_back(*clip.id);
      }
    }
    for (const std::uint32_t id : wanted) {
      CAPTURE(id);
      const AnimationClip* clip = model->findClip(id);
      REQUIRE(clip != nullptr);
      // A clip it keeps is its own; the others last and move as the donor's.
      if (std::ranges::find(recipe.keep, id) == recipe.keep.end()) {
        const AnimationClip* from = donor->findClip(id);
        REQUIRE(from != nullptr);
        CHECK(clip->tickCount == from->tickCount);
        CHECK(clip->duration == doctest::Approx(from->duration));
        CHECK(clip->repeat.has_value() == from->repeat.has_value());
        const glm::vec3 moved = dwe::animation::rootMotion(*model, *clip, clip->duration);
        const glm::vec3 donorMoved = dwe::animation::rootMotion(*donor, *from, from->duration);
        CHECK(glm::length(moved - donorMoved) < 1.0e-3F);
      }
      dwe::animation::AnimationPlayer player{*model};
      REQUIRE(player.play(*clip));
      for (int frame = 0; frame < 120; ++frame) {
        player.advance(1.0F / 60.0F);
      }
      for (const glm::mat4& joint : player.jointMatrices()) {
        CHECK(std::isfinite(joint[3][0] + joint[3][1] + joint[3][2]));
      }
    }
  }
}
