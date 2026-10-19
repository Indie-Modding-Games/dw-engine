#include <dwe/formats/model/gltf.hpp>

#include <cgltf.h>
#include <cgltf_write.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <deque>
#include <format>
#include <limits>
#include <map>
#include <numbers>
#include <optional>
#include <ranges>
#include <set>
#include <span>
#include <string>
#include <utility>

namespace dwe::formats::model {

namespace {

using Vec3 = std::array<double, 3>;
using Quat = std::array<double, 4>; // x, y, z, w, as glTF stores it

constexpr double Fixed12 = 4096.0;
constexpr Quat IdentityRotation{0.0, 0.0, 0.0, 1.0};

// A texture-modulating colour of 128 leaves the texel as it is.
constexpr double NeutralModulation = 128.0;
constexpr double FullColor = 255.0;

constexpr std::uint32_t GlbMagic = 0x46546C67; // "glTF"
constexpr std::uint32_t GlbVersion = 2;
constexpr std::uint32_t GlbJsonChunk = 0x4E4F534A; // "JSON"
constexpr std::uint32_t GlbBinChunk = 0x004E4942;  // "BIN\0"
constexpr std::size_t GlbAlignment = 4;

Quat multiply(const Quat& a, const Quat& b) {
    return {(a[3] * b[0]) + (a[0] * b[3]) + (a[1] * b[2]) - (a[2] * b[1]),
            (a[3] * b[1]) - (a[0] * b[2]) + (a[1] * b[3]) + (a[2] * b[0]),
            (a[3] * b[2]) + (a[0] * b[1]) - (a[1] * b[0]) + (a[2] * b[3]),
            (a[3] * b[3]) - (a[0] * b[0]) - (a[1] * b[1]) - (a[2] * b[2])};
}

Vec3 cross(const Vec3& a, const Vec3& b) {
    return {(a[1] * b[2]) - (a[2] * b[1]), (a[2] * b[0]) - (a[0] * b[2]), (a[0] * b[1]) - (a[1] * b[0])};
}

Vec3 rotate(const Quat& q, const Vec3& v) {
    const Vec3 axis{q[0], q[1], q[2]};
    const Vec3 c = cross(axis, v);
    const Vec3 t{2.0 * c[0], 2.0 * c[1], 2.0 * c[2]};
    const Vec3 u = cross(axis, t);
    return {v[0] + (q[3] * t[0]) + u[0], v[1] + (q[3] * t[1]) + u[1], v[2] + (q[3] * t[2]) + u[2]};
}

// The console's Y and Z point down and into the screen; glTF's point up and out of it. Turning 180 degrees about X
// takes one to the other without changing handedness, so windings and rotations keep their meaning.
Vec3 toGltf(double x, double y, double z) {
    return {x, -y, -z};
}

template <typename T>
Vec3 toGltf(const std::array<T, 3>& v) {
    return toGltf(static_cast<double>(v[0]), static_cast<double>(v[1]), static_cast<double>(v[2]));
}

// RotMatrix: Rx·Ry·Rz with 4096 = 360 degrees, then turned like the axes.
Quat toGltfRotation(const std::array<std::int16_t, 3>& angles) {
    const auto about = [&](std::size_t axis) {
        const double half = static_cast<double>(angles[axis]) * std::numbers::pi / Fixed12;
        Quat q{0.0, 0.0, 0.0, std::cos(half)};
        q[axis] = std::sin(half);
        return q;
    };
    const Quat q = multiply(multiply(about(0), about(1)), about(2));
    return {q[0], -q[1], -q[2], q[3]};
}

Vec3 toGltfScale(const std::array<std::int32_t, 3>& scale) {
    return {scale[0] / Fixed12, scale[1] / Fixed12, scale[2] / Fixed12};
}

// A transform without scale: the bind pose, where every vertex is placed.
struct Rigid {
    Quat rotation = IdentityRotation;
    Vec3 translation{};

    [[nodiscard]] Vec3 apply(const Vec3& v) const {
        const Vec3 r = rotate(rotation, v);
        return {r[0] + translation[0], r[1] + translation[1], r[2] + translation[2]};
    }
    [[nodiscard]] Rigid then(const Rigid& local) const {
        return {.rotation = multiply(rotation, local.rotation), .translation = apply(local.translation)};
    }

    // The inverse as a column-major 4x4 matrix.
    [[nodiscard]] std::array<float, 16> inverseMatrix() const {
        const Quat inverse{-rotation[0], -rotation[1], -rotation[2], rotation[3]};
        const Vec3 x = rotate(inverse, {1.0, 0.0, 0.0});
        const Vec3 y = rotate(inverse, {0.0, 1.0, 0.0});
        const Vec3 z = rotate(inverse, {0.0, 0.0, 1.0});
        const Vec3 t = rotate(inverse, translation);
        const auto f = [](double value) { return static_cast<float>(value); };
        return {f(x[0]), f(x[1]), f(x[2]), 0.0F, f(y[0]),  f(y[1]),  f(y[2]),  0.0F,
                f(z[0]), f(z[1]), f(z[2]), 0.0F, f(-t[0]), f(-t[1]), f(-t[2]), 1.0F};
    }
};

BonePose identityPose() {
    return {.scale = {4096, 4096, 4096}, .rotation = {}, .location = {}};
}

// What makes two primitives need different glTF materials.
struct MaterialKey {
    bool textured = false;
    int texture = -1; // -1: no texture given
    int palette = -1; // -1: no image
    bool semiTransparent = false;
    std::uint8_t blending = 0; // the TSB's semi-transparency mode, for textured primitives
    bool doubleSided = false;
    bool lit = true;

    auto operator<=>(const MaterialKey&) const = default;

    [[nodiscard]] bool hasColors() const { return !textured || !lit; }
};

struct Group {
    std::vector<float> positions;
    std::vector<float> normals;
    std::vector<float> uvs;
    std::vector<float> colors;
    std::vector<std::uint8_t> joints;
    std::vector<float> weights;
    std::vector<std::uint32_t> indices;
    std::uint32_t vertexCount = 0;
};

struct TextureMapping {
    int texture = -1;
    int palette = -1;
    std::array<std::array<float, 2>, 4> uvs{};
};

// Finds the texture and palette a textured primitive reads, and its UVs on that texture.
core::Result<TextureMapping> mapTexture(const TmdPrimitive& primitive, std::span<const ModelTexture> textures) {
    const std::uint8_t depth = primitive.depth();
    if (depth > TmdDirectColorDepth) {
        return core::fail(
            std::format("primitive has texture page {:#06x} with a reserved colour depth", primitive.tsb));
    }
    // Texel coordinates across VRAM in the primitive's depth, so a page and a texture can be compared.
    const int texelsPerHalfword = 4 >> depth;
    const int pageX = primitive.pageX() * texelsPerHalfword;
    const int pageY = primitive.pageY();
    const auto corners = std::span(primitive.uvs).first(primitive.vertexCount);
    const auto [minU, maxU] = std::ranges::minmax(corners | std::views::transform(&TmdUv::u));
    const auto [minV, maxV] = std::ranges::minmax(corners | std::views::transform(&TmdUv::v));

    for (std::size_t i = 0; i < textures.size(); ++i) {
        const ModelTexture& texture = textures[i];
        const int left = texture.pixelX * texelsPerHalfword;
        const int top = texture.pixelY;
        if (texture.depth != depth || pageX + minU < left || pageX + maxU > left + texture.width ||
            pageY + minV < top || pageY + maxV > top + texture.height) {
            continue;
        }
        int palette = -1;
        if (!texture.palettes.empty()) {
            const auto found = std::ranges::find_if(texture.palettes, [&](const TexturePalette& candidate) {
                return depth == TmdDirectColorDepth ||
                       (candidate.clutX == primitive.clutX() && candidate.clutY == primitive.clutY());
            });
            if (found == texture.palettes.end()) {
                continue;
            }
            palette = static_cast<int>(found - texture.palettes.begin());
        }
        TextureMapping mapping{.texture = static_cast<int>(i), .palette = palette, .uvs = {}};
        for (std::size_t c = 0; c < corners.size(); ++c) {
            mapping.uvs[c] = {static_cast<float>(pageX + corners[c].u - left) / static_cast<float>(texture.width),
                              static_cast<float>(pageY + corners[c].v - top) / static_cast<float>(texture.height)};
        }
        return mapping;
    }
    return core::fail(std::format("no texture holds texels u {}-{} v {}-{} of the {}-bit page at ({}, {}) with the "
                                  "palette at ({}, {})",
                                  minU, maxU, minV, maxV, 4 << depth, primitive.pageX(), primitive.pageY(),
                                  primitive.clutX(), primitive.clutY()));
}

// Returns the vector scaled to length 1, or fallback if it has no length.
Vec3 normalized(const Vec3& v, const Vec3& fallback) {
    const double length = std::sqrt((v[0] * v[0]) + (v[1] * v[1]) + (v[2] * v[2]));
    if (length == 0.0) {
        return fallback;
    }
    return {v[0] / length, v[1] / length, v[2] / length};
}

// Adds a primitive's corners to its group. The console draws clockwise faces as the front, glTF counter-clockwise
// ones, so the triangles are written in reverse order; a quad is the triangles (0, 1, 2) and (1, 3, 2).
core::Result<void> addPrimitive(const TmdObject& object, const TmdPrimitive& primitive, const Rigid& bind,
                                std::size_t bone, std::span<const ModelTexture> textures,
                                std::map<MaterialKey, Group>& groups) {
    MaterialKey key{.textured = primitive.isTextured(),
                    .texture = -1,
                    .palette = -1,
                    .semiTransparent = primitive.isSemiTransparent(),
                    .blending = 0,
                    .doubleSided = primitive.isDoubleSided(),
                    .lit = primitive.isLit()};
    TextureMapping mapping;
    if (key.textured) {
        key.blending = key.semiTransparent ? primitive.blending() : std::uint8_t{0};
        if (!textures.empty()) {
            auto mapped = mapTexture(primitive, textures);
            if (!mapped) {
                return std::unexpected(mapped.error());
            }
            mapping = *mapped;
            key.texture = mapping.texture;
            key.palette = mapping.palette;
        }
    }

    std::array<Vec3, 4> corners{};
    for (std::size_t i = 0; i < primitive.vertexCount; ++i) {
        corners[i] = bind.apply(toGltf(object.vertices[primitive.vertices[i]]));
    }
    const Vec3 edgeA{corners[1][0] - corners[0][0], corners[1][1] - corners[0][1], corners[1][2] - corners[0][2]};
    const Vec3 edgeB{corners[2][0] - corners[0][0], corners[2][1] - corners[0][1], corners[2][2] - corners[0][2]};
    // The faces are clockwise, so the outward face normal is B x A. It stands in for a normal of length 0.
    const Vec3 face = normalized(cross(edgeB, edgeA), {0.0, 1.0, 0.0});

    Group& group = groups[key];
    const std::uint32_t first = group.vertexCount;
    for (std::size_t i = 0; i < primitive.vertexCount; ++i) {
        for (const double value : corners[i]) {
            group.positions.push_back(static_cast<float>(value));
        }
        if (key.lit) {
            const std::size_t normal = primitive.normals[primitive.normalCount == 1 ? 0 : i];
            const Vec3 local = normalized(toGltf(object.normals[normal]), {});
            const Vec3 n = local == Vec3{} ? face : rotate(bind.rotation, local);
            for (const double value : n) {
                group.normals.push_back(static_cast<float>(value));
            }
        }
        if (key.texture >= 0) {
            group.uvs.push_back(mapping.uvs[i][0]);
            group.uvs.push_back(mapping.uvs[i][1]);
        }
        if (key.hasColors()) {
            const TmdColor& color = primitive.colors[primitive.colorCount == 1 ? 0 : i];
            const double scale = key.textured ? NeutralModulation : FullColor;
            group.colors.push_back(static_cast<float>(color.r / scale));
            group.colors.push_back(static_cast<float>(color.g / scale));
            group.colors.push_back(static_cast<float>(color.b / scale));
        }
        group.joints.insert(group.joints.end(), {static_cast<std::uint8_t>(bone), 0, 0, 0});
        group.weights.insert(group.weights.end(), {1.0F, 0.0F, 0.0F, 0.0F});
    }
    group.vertexCount += primitive.vertexCount;
    group.indices.insert(group.indices.end(), {first, first + 2, first + 1});
    if (primitive.vertexCount == 4) {
        group.indices.insert(group.indices.end(), {first + 1, first + 2, first + 3});
    }
    return {};
}

// The parts of the glTF document, by index, before they become cgltf's linked structures.
struct AccessorInfo {
    std::size_t view = 0;
    cgltf_component_type component = cgltf_component_type_invalid;
    cgltf_type type = cgltf_type_invalid;
    std::size_t count = 0;
    bool withBounds = false;
    std::array<float, 16> min{};
    std::array<float, 16> max{};
};

struct AttributeInfo {
    const char* name = nullptr;
    cgltf_attribute_type type = cgltf_attribute_type_invalid;
    std::size_t accessor = 0;
};

struct PrimitiveInfo {
    std::vector<AttributeInfo> attributes;
    std::size_t indices = 0;
    std::size_t material = 0;
};

struct ChannelInfo {
    std::size_t input = 0;
    std::size_t output = 0;
    std::size_t node = 0;
    cgltf_animation_path_type path = cgltf_animation_path_type_invalid;
};

struct AnimationInfo {
    std::string name;
    std::vector<ChannelInfo> channels;
    std::string extras;
};

struct NodeInfo {
    std::string name;
    std::vector<std::size_t> children;
    Vec3 translation{};
    Quat rotation = IdentityRotation;
    Vec3 scale{1.0, 1.0, 1.0};
};

std::size_t componentCount(cgltf_type type) {
    switch (type) {
    case cgltf_type_vec2:
        return 2;
    case cgltf_type_vec3:
        return 3;
    case cgltf_type_vec4:
        return 4;
    case cgltf_type_mat4:
        return 16;
    default:
        return 1;
    }
}

struct Document {
    // Appends bytes to the binary chunk as a buffer view, aligned to 4 bytes.
    std::size_t addView(std::span<const std::byte> bytes) {
        while (bin.size() % GlbAlignment != 0) {
            bin.push_back(std::byte{0});
        }
        views.emplace_back(bin.size(), bytes.size());
        bin.insert(bin.end(), bytes.begin(), bytes.end());
        return views.size() - 1;
    }

    template <typename T>
    std::size_t addAccessor(const std::vector<T>& values, cgltf_component_type component, cgltf_type type,
                            bool withBounds) {
        const std::size_t width = componentCount(type);
        AccessorInfo info{.view = addView(std::as_bytes(std::span(values))),
                          .component = component,
                          .type = type,
                          .count = values.size() / width,
                          .withBounds = withBounds};
        if (withBounds) {
            for (std::size_t c = 0; c < width; ++c) {
                info.min[c] = std::numeric_limits<float>::max();
                info.max[c] = std::numeric_limits<float>::lowest();
            }
            for (std::size_t i = 0; i < values.size(); ++i) {
                info.min[i % width] = std::min(info.min[i % width], static_cast<float>(values[i]));
                info.max[i % width] = std::max(info.max[i % width], static_cast<float>(values[i]));
            }
        }
        accessors.push_back(info);
        return accessors.size() - 1;
    }

    std::size_t addIndices(const std::vector<std::uint32_t>& indices, std::uint32_t vertexCount) {
        if (vertexCount <= std::numeric_limits<std::uint16_t>::max()) {
            std::vector<std::uint16_t> narrow(indices.size());
            std::ranges::transform(indices, narrow.begin(),
                                   [](std::uint32_t i) { return static_cast<std::uint16_t>(i); });
            return addAccessor(narrow, cgltf_component_type_r_16u, cgltf_type_scalar, false);
        }
        return addAccessor(indices, cgltf_component_type_r_32u, cgltf_type_scalar, false);
    }

    std::vector<std::byte> bin;
    std::vector<std::pair<std::size_t, std::size_t>> views; // offset, size
    std::vector<AccessorInfo> accessors;
    std::vector<PrimitiveInfo> primitives;
    std::vector<MaterialKey> materials;
    std::vector<std::optional<std::size_t>> materialImages;
    std::vector<std::string> materialExtras;
    std::vector<std::size_t> imageViews;
    std::vector<NodeInfo> nodes;
    std::size_t inverseBindMatrices = 0;
    std::vector<AnimationInfo> animations;
};

// Where a material's texels and palette sit in VRAM, so the original's addressing can be traced back.
std::string materialExtras(const MaterialKey& key, std::span<const ModelTexture> textures) {
    if (key.texture < 0) {
        return std::format(R"({{"blending":{}}})", key.blending);
    }
    const ModelTexture& texture = textures[static_cast<std::size_t>(key.texture)];
    std::string clut = "null";
    if (key.palette >= 0) {
        const TexturePalette& palette = texture.palettes[static_cast<std::size_t>(key.palette)];
        clut = std::format("[{},{}]", palette.clutX, palette.clutY);
    }
    return std::format(R"({{"image":[{},{}],"depth":{},"clut":{},"blending":{}}})", texture.pixelX, texture.pixelY,
                       texture.depth, clut, key.blending);
}

std::string animationExtras(const Animation& animation) {
    std::string loops;
    for (const AnimationLoop& loop : animation.loops) {
        loops += std::format(R"({}{{"firstTick":{},"lastTick":{},"count":{}}})", loops.empty() ? "" : ",",
                             loop.firstTick, loop.lastTick, loop.count);
    }
    std::string moves;
    for (const TextureMove& move : animation.textureMoves) {
        moves += std::format(R"({}{{"tick":{},"src":[{},{}],"size":[{},{}],"dst":[{},{}]}})", moves.empty() ? "" : ",",
                             move.tick, move.srcX, move.srcY, move.width, move.height, move.dstX, move.dstY);
    }
    std::string sounds;
    for (const SoundEvent& sound : animation.sounds) {
        sounds += std::format(R"({}{{"tick":{},"bank":{},"sound":{}}})", sounds.empty() ? "" : ",", sound.tick,
                              sound.bank, sound.sound);
    }
    return std::format(R"({{"id":{},"frameCount":{},"hasScale":{},"ticksPerSecond":{},"tickCount":{},)"
                       R"("endsInLoop":{},"rootMotionNode":0,"loops":[{}],"textureMoves":[{}],"sounds":[{}]}})",
                       animation.id, animation.frameCount, animation.hasScale, AnimationTicksPerSecond,
                       animation.tickCount(), animation.endsInLoop, loops, moves, sounds);
}

// Adds the channels of one animation. A channel is left out when the bone keeps its default value for the whole
// animation, the default being its node's transform.
void addAnimation(Document& document, const Animation& animation, std::span<const BonePose> defaults) {
    AnimationInfo info{
        .name = std::format("anim_{:02}", animation.id), .channels = {}, .extras = animationExtras(animation)};
    const std::uint32_t ticks = animation.tickCount();
    std::vector<float> times(ticks);
    for (std::uint32_t t = 0; t < ticks; ++t) {
        times[t] = static_cast<float>(t) / static_cast<float>(AnimationTicksPerSecond);
    }
    const std::size_t input = document.addAccessor(times, cgltf_component_type_r_32f, cgltf_type_scalar, true);

    for (std::size_t bone = 0; bone < animation.boneCount; ++bone) {
        const BonePose& rest = defaults[bone];
        bool moves = false;
        bool turns = false;
        bool scales = false;
        for (std::uint32_t t = 0; t < ticks; ++t) {
            const BonePose& pose = animation.pose(t, bone);
            moves = moves || (bone == 0 ? animation.rootMotion[t] != Vec3{} : pose.location != rest.location);
            turns = turns || pose.rotation != rest.rotation;
            scales = scales || pose.scale != rest.scale;
        }
        if (moves) {
            std::vector<float> values;
            for (std::uint32_t t = 0; t < ticks; ++t) {
                const Vec3 v = bone == 0 ? toGltf(animation.rootMotion[t]) : toGltf(animation.pose(t, bone).location);
                values.insert(values.end(),
                              {static_cast<float>(v[0]), static_cast<float>(v[1]), static_cast<float>(v[2])});
            }
            info.channels.push_back(
                {.input = input,
                 .output = document.addAccessor(values, cgltf_component_type_r_32f, cgltf_type_vec3, false),
                 .node = bone,
                 .path = cgltf_animation_path_type_translation});
        }
        if (turns) {
            std::vector<float> values;
            Quat previous = IdentityRotation;
            for (std::uint32_t t = 0; t < ticks; ++t) {
                Quat q = toGltfRotation(animation.pose(t, bone).rotation);
                // Keep neighbours in the same hemisphere so that interpolation takes the short way.
                const double dot =
                    (q[0] * previous[0]) + (q[1] * previous[1]) + (q[2] * previous[2]) + (q[3] * previous[3]);
                if (t > 0 && dot < 0.0) {
                    q = {-q[0], -q[1], -q[2], -q[3]};
                }
                previous = q;
                for (const double value : q) {
                    values.push_back(static_cast<float>(value));
                }
            }
            info.channels.push_back(
                {.input = input,
                 .output = document.addAccessor(values, cgltf_component_type_r_32f, cgltf_type_vec4, false),
                 .node = bone,
                 .path = cgltf_animation_path_type_rotation});
        }
        if (scales) {
            std::vector<float> values;
            for (std::uint32_t t = 0; t < ticks; ++t) {
                for (const double value : toGltfScale(animation.pose(t, bone).scale)) {
                    values.push_back(static_cast<float>(value));
                }
            }
            info.channels.push_back(
                {.input = input,
                 .output = document.addAccessor(values, cgltf_component_type_r_32f, cgltf_type_vec3, false),
                 .node = bone,
                 .path = cgltf_animation_path_type_scale});
        }
    }
    document.animations.push_back(std::move(info));
}

template <typename T>
void copyFloats(const T& from, std::span<float> to) {
    for (std::size_t i = 0; i < from.size(); ++i) {
        to[i] = static_cast<float>(from[i]);
    }
}

// Links the document into cgltf's structures, writes the JSON and wraps it with the binary chunk.
core::Result<std::vector<std::byte>> writeGlb(Document& document, std::size_t boneCount) {
    std::deque<std::string> strings;
    const auto text = [&](std::string value) { return strings.emplace_back(std::move(value)).data(); };

    cgltf_buffer buffer{};
    buffer.size = document.bin.size();

    std::vector<cgltf_buffer_view> views(document.views.size());
    for (std::size_t i = 0; i < views.size(); ++i) {
        views[i].buffer = &buffer;
        views[i].offset = document.views[i].first;
        views[i].size = document.views[i].second;
    }

    std::vector<cgltf_accessor> accessors(document.accessors.size());
    for (std::size_t i = 0; i < accessors.size(); ++i) {
        const AccessorInfo& info = document.accessors[i];
        cgltf_accessor& accessor = accessors[i];
        accessor.buffer_view = &views[info.view];
        accessor.component_type = info.component;
        accessor.type = info.type;
        accessor.count = info.count;
        accessor.has_min = info.withBounds ? 1 : 0;
        accessor.has_max = info.withBounds ? 1 : 0;
        std::ranges::copy(info.min, std::begin(accessor.min));
        std::ranges::copy(info.max, std::begin(accessor.max));
    }

    std::vector<cgltf_image> images(document.imageViews.size());
    std::vector<cgltf_texture> textures(images.size());
    cgltf_sampler sampler{.name = nullptr,
                          .mag_filter = cgltf_filter_type_nearest,
                          .min_filter = cgltf_filter_type_nearest,
                          .wrap_s = cgltf_wrap_mode_clamp_to_edge,
                          .wrap_t = cgltf_wrap_mode_clamp_to_edge,
                          .extras = {},
                          .extensions_count = 0,
                          .extensions = nullptr};
    for (std::size_t i = 0; i < images.size(); ++i) {
        images[i].buffer_view = &views[document.imageViews[i]];
        images[i].mime_type = text("image/png");
        textures[i].image = &images[i];
        textures[i].sampler = &sampler;
    }

    std::vector<cgltf_material> materials(document.materials.size());
    for (std::size_t i = 0; i < materials.size(); ++i) {
        const MaterialKey& key = document.materials[i];
        cgltf_material& material = materials[i];
        material.name = text(std::format("material_{:02}", i));
        material.has_pbr_metallic_roughness = 1;
        std::ranges::fill(material.pbr_metallic_roughness.base_color_factor, 1.0F);
        material.pbr_metallic_roughness.metallic_factor = 0.0F;
        material.pbr_metallic_roughness.roughness_factor = 1.0F;
        if (const auto image = document.materialImages[i]) {
            material.pbr_metallic_roughness.base_color_texture.texture = &textures[*image];
            material.pbr_metallic_roughness.base_color_texture.scale = 1.0F;
        }
        // Texel 0 of a palette is transparent on the console; a semi-transparent primitive blends.
        material.alpha_mode = cgltf_alpha_mode_opaque;
        if (key.semiTransparent) {
            material.alpha_mode = cgltf_alpha_mode_blend;
        } else if (key.textured) {
            material.alpha_mode = cgltf_alpha_mode_mask;
        }
        material.alpha_cutoff = 0.5F;
        material.double_sided = key.doubleSided ? 1 : 0;
        material.unlit = key.lit ? 0 : 1;
        material.extras.data = text(document.materialExtras[i]);
    }

    std::vector<std::vector<cgltf_attribute>> attributes(document.primitives.size());
    std::vector<cgltf_primitive> primitives(document.primitives.size());
    for (std::size_t i = 0; i < primitives.size(); ++i) {
        const PrimitiveInfo& info = document.primitives[i];
        for (const AttributeInfo& attribute : info.attributes) {
            cgltf_attribute& linked = attributes[i].emplace_back();
            linked.name = text(attribute.name);
            linked.type = attribute.type;
            linked.data = &accessors[attribute.accessor];
        }
        primitives[i].type = cgltf_primitive_type_triangles;
        primitives[i].indices = &accessors[info.indices];
        primitives[i].material = &materials[info.material];
        primitives[i].attributes = attributes[i].data();
        primitives[i].attributes_count = attributes[i].size();
    }
    cgltf_mesh mesh{};
    mesh.name = text("model");
    mesh.primitives = primitives.data();
    mesh.primitives_count = primitives.size();

    // The bones, then the node of the skinned mesh. glTF ignores a skinned mesh node's transform, so it sits at the
    // root of the scene.
    std::vector<cgltf_node> nodes(document.nodes.size() + 1);
    std::vector<std::vector<cgltf_node*>> children(nodes.size());
    for (std::size_t i = 0; i < document.nodes.size(); ++i) {
        const NodeInfo& info = document.nodes[i];
        cgltf_node& node = nodes[i];
        node.name = text(info.name);
        for (const std::size_t child : info.children) {
            children[i].push_back(&nodes[child]);
            nodes[child].parent = &node;
        }
        node.children = children[i].data();
        node.children_count = children[i].size();
        node.has_translation = 1;
        node.has_rotation = 1;
        node.has_scale = 1;
        copyFloats(info.translation, std::span<float>(node.translation));
        copyFloats(info.rotation, std::span<float>(node.rotation));
        copyFloats(info.scale, std::span<float>(node.scale));
    }

    std::vector<cgltf_node*> joints;
    std::vector<cgltf_node*> roots;
    for (std::size_t i = 0; i < boneCount; ++i) {
        joints.push_back(&nodes[i]);
        if (nodes[i].parent == nullptr) {
            roots.push_back(&nodes[i]);
        }
    }
    cgltf_skin skin{};
    skin.joints = joints.data();
    skin.joints_count = joints.size();
    skin.skeleton = roots.size() == 1 ? roots.front() : nullptr;
    skin.inverse_bind_matrices = &accessors[document.inverseBindMatrices];

    cgltf_node& meshNode = nodes.back();
    meshNode.name = text("model");
    meshNode.mesh = &mesh;
    meshNode.skin = &skin;
    roots.push_back(&meshNode);

    cgltf_scene scene{};
    scene.nodes = roots.data();
    scene.nodes_count = roots.size();

    std::vector<std::vector<cgltf_animation_sampler>> samplers(document.animations.size());
    std::vector<std::vector<cgltf_animation_channel>> channels(document.animations.size());
    std::vector<cgltf_animation> animations(document.animations.size());
    for (std::size_t i = 0; i < animations.size(); ++i) {
        const AnimationInfo& info = document.animations[i];
        samplers[i].resize(info.channels.size());
        channels[i].resize(info.channels.size());
        for (std::size_t c = 0; c < info.channels.size(); ++c) {
            samplers[i][c].input = &accessors[info.channels[c].input];
            samplers[i][c].output = &accessors[info.channels[c].output];
            samplers[i][c].interpolation = cgltf_interpolation_type_linear;
            channels[i][c].sampler = &samplers[i][c];
            channels[i][c].target_node = &nodes[info.channels[c].node];
            channels[i][c].target_path = info.channels[c].path;
        }
        animations[i].name = text(info.name);
        animations[i].samplers = samplers[i].data();
        animations[i].samplers_count = samplers[i].size();
        animations[i].channels = channels[i].data();
        animations[i].channels_count = channels[i].size();
        animations[i].extras.data = text(info.extras);
    }

    cgltf_data data{};
    data.asset.version = text("2.0");
    data.asset.generator = text("dwx");
    data.asset.extras.data =
        text(R"({"axes":"console Y down turned 180 degrees about X","units":"original model units"})");
    data.buffers = &buffer;
    data.buffers_count = 1;
    data.buffer_views = views.data();
    data.buffer_views_count = views.size();
    data.accessors = accessors.data();
    data.accessors_count = accessors.size();
    data.images = images.data();
    data.images_count = images.size();
    data.samplers = images.empty() ? nullptr : &sampler;
    data.samplers_count = images.empty() ? 0 : 1;
    data.textures = textures.data();
    data.textures_count = textures.size();
    data.materials = materials.data();
    data.materials_count = materials.size();
    data.meshes = &mesh;
    data.meshes_count = 1;
    data.skins = &skin;
    data.skins_count = 1;
    data.nodes = nodes.data();
    data.nodes_count = nodes.size();
    data.scenes = &scene;
    data.scenes_count = 1;
    data.scene = &scene;
    data.animations = animations.data();
    data.animations_count = animations.size();

    const cgltf_options options{};
    const cgltf_size jsonSize = cgltf_write(&options, nullptr, 0, &data);
    std::string json(jsonSize, '\0');
    if (jsonSize == 0 || cgltf_write(&options, json.data(), jsonSize, &data) != jsonSize) {
        return core::fail("could not write the glTF document");
    }
    json.resize(jsonSize - 1); // without the terminating NUL
    while (json.size() % GlbAlignment != 0) {
        json.push_back(' ');
    }
    std::vector<std::byte> bin = std::move(document.bin);
    while (bin.size() % GlbAlignment != 0) {
        bin.push_back(std::byte{0});
    }

    const std::size_t total = 12 + 8 + json.size() + 8 + bin.size();
    if (total > std::numeric_limits<std::uint32_t>::max()) {
        return core::fail(std::format("the model takes {} bytes, more than a GLB can hold", total));
    }
    std::vector<std::byte> glb;
    glb.reserve(total);
    const auto u32 = [&](std::size_t value) {
        for (unsigned shift = 0; shift < 32; shift += 8) {
            glb.push_back(static_cast<std::byte>((value >> shift) & 0xFFU));
        }
    };
    u32(GlbMagic);
    u32(GlbVersion);
    u32(total);
    u32(json.size());
    u32(GlbJsonChunk);
    std::ranges::transform(json, std::back_inserter(glb), [](char c) { return static_cast<std::byte>(c); });
    u32(bin.size());
    u32(GlbBinChunk);
    glb.insert(glb.end(), bin.begin(), bin.end());
    return glb;
}

} // namespace

core::Result<std::vector<std::byte>> writeModelGlb(const Tmd& tmd, std::span<const SkeletonBone> skeleton,
                                                   std::span<const Animation> animations,
                                                   std::span<const ModelTexture> textures) {
    if (auto valid = validateSkeleton(skeleton, tmd.objects.size()); !valid) {
        return std::unexpected(valid.error());
    }
    for (const Animation& animation : animations) {
        if (animation.boneCount != skeleton.size()) {
            return core::fail(std::format("animation {} has {} bones but the skeleton has {}", animation.id,
                                          animation.boneCount, skeleton.size()));
        }
    }
    if (skeleton.size() > MaxAnimationBones) {
        return core::fail(std::format("a skeleton of {} bones is not supported", skeleton.size()));
    }
    if (std::ranges::any_of(textures,
                            [](const ModelTexture& texture) { return texture.width == 0 || texture.height == 0; })) {
        return core::fail("a texture has no size");
    }

    // The default pose is the first animation's first tick, as the original shows a model before it animates. The
    // bind pose is the same without scale, so it can always be inverted.
    std::vector<BonePose> defaults(skeleton.size(), identityPose());
    if (!animations.empty()) {
        for (std::size_t bone = 1; bone < skeleton.size(); ++bone) {
            defaults[bone] = animations.front().pose(0, bone);
        }
    }

    Document document;
    std::vector<Rigid> bind(skeleton.size());
    std::vector<float> inverseBind;
    for (std::size_t bone = 0; bone < skeleton.size(); ++bone) {
        const BonePose& pose = defaults[bone];
        const Rigid local{.rotation = toGltfRotation(pose.rotation), .translation = toGltf(pose.location)};
        const std::int8_t parent = skeleton[bone].parentIndex;
        bind[bone] = parent < 0 ? local : bind[static_cast<std::size_t>(parent)].then(local);
        const auto matrix = bind[bone].inverseMatrix();
        inverseBind.insert(inverseBind.end(), matrix.begin(), matrix.end());

        document.nodes.push_back({.name = std::format("bone_{:02}", bone),
                                  .children = {},
                                  .translation = local.translation,
                                  .rotation = local.rotation,
                                  .scale = toGltfScale(pose.scale)});
        if (parent >= 0) {
            document.nodes[static_cast<std::size_t>(parent)].children.push_back(bone);
        }
    }
    document.inverseBindMatrices =
        document.addAccessor(inverseBind, cgltf_component_type_r_32f, cgltf_type_mat4, false);

    std::map<MaterialKey, Group> groups;
    for (std::size_t bone = 0; bone < skeleton.size(); ++bone) {
        if (skeleton[bone].objIndex < 0) {
            continue;
        }
        const TmdObject& object = tmd.objects[static_cast<std::size_t>(skeleton[bone].objIndex)];
        for (const TmdPrimitive& primitive : object.primitives) {
            if (auto added = addPrimitive(object, primitive, bind[bone], bone, textures, groups); !added) {
                return std::unexpected(added.error());
            }
        }
    }

    // One image per texture and palette in use, in that order. Palettes are only set when a texture has them.
    std::map<std::pair<int, int>, std::size_t> imageOf;
    for (const auto& [key, group] : groups) {
        if (key.palette >= 0) {
            imageOf.emplace(std::pair{key.texture, key.palette}, 0);
        }
    }
    for (auto& [where, image] : imageOf) {
        image = document.imageViews.size();
        const ModelTexture& texture = textures[static_cast<std::size_t>(where.first)];
        document.imageViews.push_back(document.addView(texture.palettes[static_cast<std::size_t>(where.second)].png));
    }
    for (const auto& [key, group] : groups) {
        std::optional<std::size_t> image;
        if (key.palette >= 0) {
            image = imageOf.find(std::pair{key.texture, key.palette})->second;
        }
        document.materials.push_back(key);
        document.materialImages.push_back(image);
        document.materialExtras.push_back(materialExtras(key, textures));

        PrimitiveInfo primitive{.attributes = {}, .indices = 0, .material = document.materials.size() - 1};
        primitive.attributes.push_back(
            {.name = "POSITION",
             .type = cgltf_attribute_type_position,
             .accessor = document.addAccessor(group.positions, cgltf_component_type_r_32f, cgltf_type_vec3, true)});
        if (!group.normals.empty()) {
            primitive.attributes.push_back(
                {.name = "NORMAL",
                 .type = cgltf_attribute_type_normal,
                 .accessor = document.addAccessor(group.normals, cgltf_component_type_r_32f, cgltf_type_vec3, false)});
        }
        if (!group.uvs.empty()) {
            primitive.attributes.push_back(
                {.name = "TEXCOORD_0",
                 .type = cgltf_attribute_type_texcoord,
                 .accessor = document.addAccessor(group.uvs, cgltf_component_type_r_32f, cgltf_type_vec2, false)});
        }
        if (!group.colors.empty()) {
            primitive.attributes.push_back(
                {.name = "COLOR_0",
                 .type = cgltf_attribute_type_color,
                 .accessor = document.addAccessor(group.colors, cgltf_component_type_r_32f, cgltf_type_vec3, false)});
        }
        primitive.attributes.push_back(
            {.name = "JOINTS_0",
             .type = cgltf_attribute_type_joints,
             .accessor = document.addAccessor(group.joints, cgltf_component_type_r_8u, cgltf_type_vec4, false)});
        primitive.attributes.push_back(
            {.name = "WEIGHTS_0",
             .type = cgltf_attribute_type_weights,
             .accessor = document.addAccessor(group.weights, cgltf_component_type_r_32f, cgltf_type_vec4, false)});
        primitive.indices = document.addIndices(group.indices, group.vertexCount);
        document.primitives.push_back(std::move(primitive));
    }
    for (const Animation& animation : animations) {
        addAnimation(document, animation, defaults);
    }
    return writeGlb(document, skeleton.size());
}

} // namespace dwe::formats::model
