#include <dwe/assets/partner_clips.hpp>
#include <dwe/assets/virtual_file_system.hpp>
#include <dwe/assets/yaml_reader.hpp>
#include <dwe/core/yaml.hpp>

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <utility>

namespace dwe::assets {

namespace {

using core::YamlNode;

constexpr std::string_view Context = "partner clips";
constexpr std::string_view SameBones = "same";

// The most clips or joints a list may name: the extracted models stay far below.
constexpr std::uint32_t MaxClipId = 255;
constexpr std::uint32_t MaxJoint = 255;

struct KindName {
  std::string_view name;
  MotionKind kind;
};

constexpr std::array<KindName, 13> KindNames{{
    {.name = "breathe", .kind = MotionKind::Breathe},
    {.name = "stride", .kind = MotionKind::Stride},
    {.name = "bounce", .kind = MotionKind::Bounce},
    {.name = "shake", .kind = MotionKind::Shake},
    {.name = "nod", .kind = MotionKind::Nod},
    {.name = "lie", .kind = MotionKind::Lie},
    {.name = "squat", .kind = MotionKind::Squat},
    {.name = "droop", .kind = MotionKind::Droop},
    {.name = "guard", .kind = MotionKind::Guard},
    {.name = "recoil", .kind = MotionKind::Recoil},
    {.name = "fall", .kind = MotionKind::Fall},
    {.name = "rise", .kind = MotionKind::Rise},
    {.name = "lunge", .kind = MotionKind::Lunge},
}};

std::uint32_t boundedId(YamlReader& reader, const YamlNode& node, std::uint32_t most) {
  const auto value = reader.integer<std::uint32_t>(node);
  if (value > most) {
    reader.fail(node, std::format("must be at most {}", most));
  }
  return value;
}

// A mapping key that is a clip id or a joint: the YAML keys are text.
std::uint32_t keyId(YamlReader& reader, const YamlNode& at, std::string_view key,
                    std::uint32_t most) {
  std::uint32_t value = 0;
  const auto [end, error] = std::from_chars(key.data(), key.data() + key.size(), value);
  if (error != std::errc{} || end != key.data() + key.size() || key.empty() || value > most) {
    reader.fail(at, std::format("'{}' must be a whole number from 0 to {}", key, most));
  }
  return value;
}

std::vector<std::uint32_t> idList(YamlReader& reader, const YamlNode& node, std::string_view what,
                                  std::uint32_t most) {
  std::vector<std::uint32_t> out;
  for (const YamlNode& item : reader.sequence(node, what)) {
    out.push_back(boundedId(reader, item, most));
  }
  return out;
}

MotionRecipe readMotion(YamlReader& reader, const YamlNode& node) {
  MotionRecipe motion;
  reader.expectKeys(node, "motion", {"kind"},
                    {"beats", "angle", "height", "lean", "strike", "from_behind"});
  const std::string kind = reader.text(reader.field(node, "kind"));
  const auto found = std::ranges::find(KindNames, kind, &KindName::name);
  if (found == KindNames.end()) {
    reader.fail(node, std::format("'{}' is not a motion kind", kind));
  } else {
    motion.kind = found->kind;
  }
  if (const YamlNode* value = node.find("beats")) {
    motion.beats = reader.integer<std::int32_t>(*value);
    if (motion.beats < 1) {
      reader.fail(*value, "beats must be 1 or more");
    }
  }
  const auto numberAt = [&](std::string_view key) {
    const YamlNode* value = node.find(key);
    return value != nullptr ? reader.number(*value) : 0.0F;
  };
  motion.angle = numberAt("angle");
  motion.height = numberAt("height");
  motion.lean = numberAt("lean");
  motion.strike = numberAt("strike");
  if (motion.strike < 0.0F || motion.strike > 1.0F) {
    reader.fail(node, "strike must be from 0 to 1");
  }
  if (const YamlNode* value = node.find("from_behind")) {
    motion.fromBehind = reader.boolean(*value);
  }
  return motion;
}

std::map<std::uint32_t, MotionRecipe> readMotions(YamlReader& reader, const YamlNode& node) {
  std::map<std::uint32_t, MotionRecipe> out;
  if (!node.isMapping()) {
    reader.fail(node, "motions must map clip ids to motions");
    return out;
  }
  for (const core::YamlEntry& entry : node.entries()) {
    out.insert_or_assign(keyId(reader, entry.value, entry.key, MaxClipId),
                         readMotion(reader, entry.value));
  }
  return out;
}

SpinBones readSpin(YamlReader& reader, const YamlNode& node) {
  SpinBones spin;
  reader.expectKeys(node, "spin", {"joints", "axis", "turns_per_second"});
  spin.joints = idList(reader, reader.field(node, "joints"), "joints", MaxJoint);
  const std::string axis = reader.text(reader.field(node, "axis"));
  if (axis == "x") {
    spin.axis = SpinAxis::X;
  } else if (axis == "y") {
    spin.axis = SpinAxis::Y;
  } else if (axis == "z") {
    spin.axis = SpinAxis::Z;
  } else {
    reader.fail(node, "axis must be x, y or z");
  }
  spin.turnsPerSecond = reader.number(reader.field(node, "turns_per_second"));
  return spin;
}

BoneGroups readGroups(YamlReader& reader, const YamlNode& node) {
  BoneGroups groups;
  reader.expectKeys(node, "groups", {"body"},
                    {"head", "left_arm", "right_arm", "left_leg", "right_leg", "spin", "upright"});
  groups.body = boundedId(reader, reader.field(node, "body"), MaxJoint);
  const auto listAt = [&](std::string_view key) {
    const YamlNode* value = node.find(key);
    return value != nullptr ? idList(reader, *value, key, MaxJoint) : std::vector<std::uint32_t>{};
  };
  groups.head = listAt("head");
  groups.leftArm = listAt("left_arm");
  groups.rightArm = listAt("right_arm");
  groups.leftLeg = listAt("left_leg");
  groups.rightLeg = listAt("right_leg");
  if (const YamlNode* value = node.find("spin")) {
    groups.spin = readSpin(reader, *value);
  }
  if (const YamlNode* value = node.find("upright")) {
    groups.upright = reader.boolean(*value);
  }
  return groups;
}

PartnerClipRecipe readRecipe(YamlReader& reader, const YamlNode& node) {
  PartnerClipRecipe recipe;
  reader.expectKeys(node, "species", {"donor", "bones"}, {"mode", "keep", "groups", "motions"});
  recipe.donor = reader.text(reader.field(node, "donor"));
  if (const YamlNode* value = node.find("mode")) {
    const std::string mode = reader.text(*value);
    if (mode == "retarget") {
      recipe.mode = PartnerClipMode::Retarget;
    } else if (mode == "procedural") {
      recipe.mode = PartnerClipMode::Procedural;
    } else {
      reader.fail(*value, "mode must be retarget or procedural");
    }
  }
  const YamlNode& bones = reader.field(node, "bones");
  if (bones.isScalar() && bones.text() == SameBones) {
    recipe.sameBones = true;
  } else if (bones.isMapping()) {
    for (const core::YamlEntry& entry : bones.entries()) {
      recipe.bones.insert_or_assign(keyId(reader, entry.value, entry.key, MaxJoint),
                                    boundedId(reader, entry.value, MaxJoint));
    }
  } else {
    reader.fail(bones, "bones must be 'same' or map joints to the donor's joints");
  }
  if (const YamlNode* value = node.find("keep")) {
    recipe.keep = idList(reader, *value, "keep", MaxClipId);
  }
  if (const YamlNode* value = node.find("groups")) {
    recipe.groups = readGroups(reader, *value);
  }
  if (const YamlNode* value = node.find("motions")) {
    recipe.motions = readMotions(reader, *value);
  }
  if (recipe.mode == PartnerClipMode::Procedural && !recipe.groups) {
    reader.fail(node, "a procedural recipe needs its bone groups");
  }
  return recipe;
}

} // namespace

std::optional<std::uint32_t> PartnerClipRecipe::donorJoint(std::uint32_t joint) const {
  if (sameBones) {
    return joint;
  }
  const auto found = bones.find(joint);
  return found != bones.end() ? std::optional{found->second} : std::nullopt;
}

const PartnerClipRecipe* PartnerClipBook::find(std::string_view id) const {
  const auto found = species.find(id);
  return found != species.end() ? &found->second : nullptr;
}

std::optional<MotionRecipe> PartnerClipBook::motionOf(const PartnerClipRecipe& recipe,
                                                      std::uint32_t clip) const {
  if (const auto own = recipe.motions.find(clip); own != recipe.motions.end()) {
    return own->second;
  }
  if (const auto found = motions.find(clip); found != motions.end()) {
    return found->second;
  }
  if (clip >= firstTechnique) {
    return technique;
  }
  return std::nullopt;
}

core::Result<PartnerClipBook> parsePartnerClipBook(std::string_view yaml) {
  auto root = core::parseYaml(yaml);
  if (!root) {
    return std::unexpected{root.error()};
  }
  YamlReader reader;
  PartnerClipBook book;
  if (reader.expectKeys(*root, Context,
                        {"clips", "first_technique", "motions", "technique", "species"})) {
    book.clips = idList(reader, reader.field(*root, "clips"), "clips", MaxClipId);
    book.firstTechnique = boundedId(reader, reader.field(*root, "first_technique"), MaxClipId);
    book.motions = readMotions(reader, reader.field(*root, "motions"));
    book.technique = readMotion(reader, reader.field(*root, "technique"));
    const YamlNode& species = reader.field(*root, "species");
    if (species.isMapping()) {
      for (const core::YamlEntry& entry : species.entries()) {
        book.species.insert_or_assign(entry.key, readRecipe(reader, entry.value));
      }
    } else {
      reader.fail(species, "species must map species ids to recipes");
    }
  }
  return reader.finish(std::move(book), Context);
}

core::Result<PartnerClipBook> loadPartnerClipBook(const VirtualFileSystem& vfs, const AssetId& id) {
  const auto bytes = vfs.read(id);
  if (!bytes) {
    return std::unexpected{bytes.error()};
  }
  auto book = parsePartnerClipBook({reinterpret_cast<const char*>(bytes->data()), bytes->size()});
  if (!book) {
    return core::fail(std::format("{}: {}", id.str(), book.error().message));
  }
  return book;
}

} // namespace dwe::assets
