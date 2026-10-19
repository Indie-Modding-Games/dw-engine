#include <dwe/formats/effect/efe_script.hpp>

#include <array>

// The sub-ops of opcode 0x0E, from their handlers in battle_effect.c (and efe.c for codes 7 and 18). Arguments are in
// push order; the comment after each name is what the handler does with it when the name alone does not say.
namespace dwe::formats::effect {

namespace {

using enum EfeArgKind;

constexpr EfeArg value(std::string_view name) {
    return {.name = name, .kind = Value, .related = 0};
}

constexpr EfeArg arg(std::string_view name, EfeArgKind kind, std::uint8_t related = 0) {
    return {.name = name, .kind = kind, .related = related};
}

constexpr std::array<EfeArg, 0> None{};
constexpr std::array A0 = {arg("bone_offsets", BoneOffsets)}; // the entry of the attacker's type; none ends the effect
// unknown0 goes to EfeUvAnim.unk0, which only BTL_unloadEFESlot reads, to clear it.
constexpr std::array A1 = {value("object_index"), value("unknown0"), arg("keyframes", UvKeyframes)};
constexpr std::array A2 = {arg("tick_script", Code), arg("render_script", Code)};
constexpr std::array A3 = {value("object_index"), arg("scale", Vector)};
constexpr std::array A4 = {arg("position", Vector), arg("color", Color), value("scale_x"), value("scale_y"),
                           value("u_offset"),       value("v_offset"),   value("width"),   value("height")};
constexpr std::array A6 = {arg("extent", Vector), arg("hit", Int32)};
constexpr std::array A7 = {arg("rotation", Vector), arg("vector", Vector)};
constexpr std::array A8 = {value("bone_index"), value("forward_offset")};
constexpr std::array A10 = {arg("out", Vector), value("scatter")};
constexpr std::array A11 = {arg("unused0", Unused), arg("unused1", Unused)};
constexpr std::array A12 = {arg("from", Vector), arg("to", Vector), value("t0"),
                            value("t1"),         value("t"),        arg("out", Vector)};
constexpr std::array A13 = {arg("target", Vector), value("speed"), value("max_turn")};
constexpr std::array AOutVector = {arg("out", Vector)};
constexpr std::array A15 = {value("object_index"), arg("color", Color)};
constexpr std::array A16 = {value("type"), value("strength"), arg("color", Color), value("frames")};
constexpr std::array A17 = {arg("index", Int32)}; // the next valid target, -1 when there are no more
constexpr std::array A18 = {
    value("mode"),        arg("position", Vector), value("fixed_depth"),      value("duration"),
    value("scale_start"), value("scale_end"),      arg("color_start", Color), arg("color_end", Color)};
constexpr std::array APosition = {arg("position", Vector)};
constexpr std::array A20 = {value("x"),       value("y"),       value("u_offset"), value("v_offset"), value("width"),
                            value("height"),  value("pivot_x"), value("pivot_y"),  value("rotate"),   value("depth"),
                            value("scale_x"), value("scale_y"), value("clut_row")};
constexpr std::array A21 = {arg("position", Vector), arg("x", Int32), arg("y", Int32), arg("depth", Int32)};
constexpr std::array A22 = {value("x"),      value("y"),     value("u_offset"), value("v_offset"), value("size_x"),
                            value("size_y"), value("scale"), value("depth"),    value("clut_row")};
constexpr std::array A24 = {arg("position", Vector), arg("color", Color), value("scale_x"), value("scale_y"),
                            value("u_offset"),       value("v_offset"),   value("width"),   value("height"),
                            value("pivot_x"),        value("pivot_y"),    value("rotate"),  value("attribute"),
                            value("clut_row")};
constexpr std::array A25 = {arg("out", Vector), value("angle"), value("radius")};
constexpr std::array A26 = {arg("destination", ParentField, 2), value("parent_offset"), value("size")};
constexpr std::array A27 = {value("timer")};
constexpr std::array A28 = {value("sound")}; // a sound of bank 8; negative plays nothing
constexpr std::array A30 = {arg("tile_map", TileMap), arg("tile_offsets", TileOffsets, 0), value("scroll_x"),
                            value("scroll_y"), value("brightness")};
constexpr std::array A31 = {arg("sprites", ParallaxSprites), value("offset_x"), value("offset_y")};
constexpr std::array ABone = {value("bone_index")};
constexpr std::array ATarget = {arg("target", Vector)};
constexpr std::array A34 = {arg("offset", Vector), arg("angle", Int32), arg("hit", Int32), value("radius")};
constexpr std::array A35 = {arg("out", Int32), value("object_index")};
constexpr std::array A36 = {arg("target", Vector), value("min_speed"),  value("max_speed"),
                            arg("unused", Unused), arg("speed", Int32), value("max_turn")};
constexpr std::array ATrig = {value("angle"), value("scale"), arg("out", Int32)};
constexpr std::array A40 = {arg("out", Int32), value("t0"), value("t1"), value("t"), value("start"), value("end")};
constexpr std::array A41 = {arg("out", Int32), value("min"), value("max")};
constexpr std::array A42 = {value("value")};
constexpr std::array A43 = {arg("out", Vector), arg("direction", Vector)};
constexpr std::array A44 = {arg("found", Int32), value("mode"), arg("extent", Vector)};
constexpr std::array ANormalize = {arg("out", Vector), arg("rotation", Vector)};
constexpr std::array A47 = {arg("out", Int32), arg("vector", Vector)};
constexpr std::array A48 = {arg("destination", Vector), arg("source", Vector)};
constexpr std::array AVectors = {arg("a", Vector), arg("b", Vector)}; // a = a <op> b
constexpr std::array ABoneTransform = {value("bone_index"), arg("position", Vector), arg("rotation", Vector)};
constexpr std::array A60 = {value("x0"),         value("y0"),    value("x1"),   value("y1"),
                            arg("color", Color), value("depth"), value("flags")};
constexpr std::array A61 = {arg("source", ParentField, 2), value("parent_offset"), value("size")};
constexpr std::array A65 = {arg("x_or_corner0", QuadCorner, 11),
                            arg("y_or_corner1", QuadCorner, 11),
                            arg("width_or_corner2", QuadCorner, 11),
                            arg("height_or_corner3", QuadCorner, 11),
                            value("depth"),
                            arg("color", Color),
                            value("u_offset"),
                            value("v_offset"),
                            value("texture_width"),
                            value("texture_height"),
                            value("clut_row"),
                            value("semi_transparency")};
constexpr std::array A66 = {arg("origin", Vector), arg("rotation", Vector),
                            arg("color", Color),   value("x1"),
                            value("x2"),           value("count_x"),
                            value("z1"),           value("z2"),
                            value("count_z"),      value("y")};
constexpr std::array A67 = {arg("unused", Unused)};
constexpr std::array A68 = {arg("center", Vector), arg("extent", Vector), arg("color", Color)};
constexpr std::array A70 = {arg("p0", Vector), arg("p1", Vector),         arg("p2", Vector),
                            arg("p3", Vector), arg("color", Color),       value("u_offset"),
                            value("v_offset"), value("texture_width"),    value("texture_height"),
                            value("clut_row"), value("semi_transparency")};
constexpr std::array AScalar = {arg("vector", Vector), value("scalar")};
constexpr std::array A74 = {arg("position_out", Vector), arg("rotation_out", Vector), arg("position", Vector),
                            arg("rotation", Vector)};
constexpr std::array A75 = {value("allow_self")};
constexpr std::array A78 = {value("clut_row"), value("clut_x"), value("count"), arg("colors", ClutColors, 2)};
constexpr std::array A80 = {value("depth"), value("clut_row"), arg("colors", ClutRow, 3), value("color_index")};
constexpr std::array AOutInt = {arg("out", Int32)};
constexpr std::array A82 = {arg("scale", Vector)};
constexpr std::array APoints = {arg("points", RibbonPoints)};
// unknown0 goes to EFE_RIBBON_SCRATCH->width, which nothing reads.
constexpr std::array A85 = {arg("points", RibbonPoints), arg("scale", Vector), value("unknown0")};
constexpr std::array A86 = {arg("center", Vector),         value("ring_count"),          arg("radii", Int32Array, 1),
                            arg("heights", Int32Array, 1), arg("colors", Int32Array, 1), value("texture_phase")};
constexpr std::array A87 = {value("depth"), arg("color", Color), value("blend")};
constexpr std::array A89 = {arg("height", Vector), arg("hit", Int32), arg("unused", Unused)}; // only height.y is read
constexpr std::array A90 = {value("radius"), arg("hit", Int32)};
constexpr std::array A91 = {arg("extent", Vector), arg("hit", Int32), value("sub_effect")};
constexpr std::array A92 = {value("brightness")};

constexpr std::array<EfeSubOpSpec, EfeSubOpCount> SubOps = {{
    {.name = "check_tech_compatibility", .handler = "BTL_checkTechCompatibility", .args = A0},
    {.name = "init_uv_anim", .handler = "BTL_initializeUVAnim", .args = A1},
    {.name = "set_sub_effect_scripts", .handler = "BTL_initializeSubEffectInstructions", .args = A2},
    {.name = "draw_tmd", .handler = "BTL_drawTMD", .args = A3},
    {.name = "render_centered_sprite", .handler = "BTL_renderCenteredSprite", .args = A4},
    {.name = "init_transform", .handler = "BTL_initializeEFETransform", .args = None},
    {.name = "check_collision", .handler = "BTL_checkCollisionWithDefaultPower", .args = A6},
    {.name = "rotate_vector_zyx", .handler = "EFERotateVector", .args = A7},
    {.name = "set_transform_to_target_bone", .handler = "BTL_setTransformToTargetBone", .args = A8},
    {.name = "hit_target", .handler = "BTL_addAttackObjectToTarget", .args = None},
    {.name = "get_scattered_spawn_position", .handler = "BTL_getScatteredSpawnPosition", .args = A10},
    {.name = "discard_pair", .handler = "BTL_discardEFEOperandPair", .args = A11},
    {.name = "interpolate_vector", .handler = "BTL_interpolateVector", .args = A12},
    {.name = "steer_toward_point", .handler = "BTL_steerTransformTowardPoint", .args = A13},
    {.name = "get_target_position", .handler = "BTL_copyTargetEntityPosition", .args = AOutVector},
    {.name = "set_model_object_color", .handler = "BTL_setEFEModelObjectColor", .args = A15},
    {.name = "add_particle_emitter", .handler = "BTL_addParticleEmitter", .args = A16},
    {.name = "select_next_target", .handler = "BTL_selectNextTargetEntity", .args = A17},
    {.name = "create_flash", .handler = "EFECreateFlash", .args = A18},
    {.name = "add_cloud", .handler = "BTL_addCloudEffect", .args = APosition},
    {.name = "render_screen_sprite", .handler = "BTL_renderScreenSprite", .args = A20},
    {.name = "project_to_screen", .handler = "BTL_projectPositionToScreen", .args = A21},
    {.name = "render_particle_flash", .handler = "BTL_renderParticleFlashSprite", .args = A22},
    {.name = "get_target_size", .handler = "BTL_getTargetDigimonSize", .args = AOutVector},
    {.name = "render_projected_sprite", .handler = "BTL_renderProjectedSprite", .args = A24},
    {.name = "polar_offset", .handler = "BTL_calculatePolarOffset", .args = A25},
    {.name = "copy_from_parent", .handler = "BTL_copyFromParentTransform", .args = A26},
    {.name = "add_source_particles", .handler = "BTL_addSourceEntityParticleFX", .args = A27},
    {.name = "play_sound", .handler = "BTL_playEFESound", .args = A28},
    {.name = "set_transform_to_bone_offset", .handler = "BTL_setTransformToBoneOffset", .args = None},
    {.name = "render_scrolling_background", .handler = "BTL_renderScrollingBackground", .args = A30},
    {.name = "render_parallax_sprites", .handler = "BTL_renderParallaxSprites", .args = A31},
    {.name = "set_transform_to_source_bone", .handler = "BTL_setTransformToSourceBone", .args = ABone},
    {.name = "face_point", .handler = "BTL_rotateTransformTowardPoint", .args = ATarget},
    {.name = "check_target_collision", .handler = "BTL_checkTargetCollision", .args = A34},
    {.name = "get_uv_anim_frame", .handler = "BTL_getUVAnimTimer", .args = A35},
    {.name = "home_toward_point", .handler = "BTL_applyHomingMovement", .args = A36},
    {.name = "get_source_size", .handler = "BTL_getSourceDigimonSize", .args = AOutVector},
    {.name = "sine", .handler = "BTL_calculateSine", .args = ATrig},
    {.name = "cosine", .handler = "BTL_calculateCosine", .args = ATrig},
    {.name = "interpolate_value", .handler = "BTL_interpolateValue", .args = A40},
    {.name = "random_in_range", .handler = "BTL_getRandomInRange", .args = A41},
    {.name = "print_debug_value", .handler = "BTL_printDebugValue", .args = A42},
    {.name = "get_euler_angles", .handler = "BTL_getVectorEulerAngles", .args = A43},
    {.name = "find_hit_entity", .handler = "BTL_findHitEntity", .args = A44},
    {.name = "normalize_rotation", .handler = "BTL_normalizeRotationAngles", .args = ANormalize},
    {.name = "target_hit_entity", .handler = "BTL_setTargetToHitEntity", .args = None},
    {.name = "vector_length", .handler = "BTL_getVectorLength", .args = A47},
    {.name = "copy_vector", .handler = "BTL_copyVector", .args = A48},
    {.name = "add_vectors", .handler = "BTL_addVectors", .args = AVectors},
    {.name = "subtract_vectors", .handler = "BTL_subtractVectors", .args = AVectors},
    {.name = "multiply_vectors", .handler = "BTL_multiplyVectors", .args = AVectors},
    {.name = "divide_vectors", .handler = "BTL_divideVectors", .args = AVectors},
    {.name = "mask_vectors", .handler = "BTL_maskVectors", .args = AVectors},
    {.name = "shift_vectors_right", .handler = "BTL_shiftVectorsRight", .args = AVectors},
    {.name = "center_on_entities", .handler = "BTL_centerTransformOnEntities", .args = None},
    {.name = "get_target_bone_transform", .handler = "BTL_getTargetBoneTransform", .args = ABoneTransform},
    {.name = "rotate_vector_yxz", .handler = "BTL_rotateVectorByAngles", .args = A7},
    {.name = "normalize_rotation_2", .handler = "BTL_normalizeRotationAngles2", .args = ANormalize},
    {.name = "combine_rotations", .handler = "BTL_combineRotations", .args = AVectors},
    {.name = "render_line", .handler = "BTL_renderEFELine", .args = A60},
    {.name = "copy_to_parent", .handler = "BTL_copyToParentTransform", .args = A61},
    {.name = "get_source_bone_transform", .handler = "BTL_getSourceBoneTransform", .args = ABoneTransform},
    {.name = "setup_fixed_camera", .handler = "BTL_setupFixedCamera", .args = None},
    {.name = "restore_camera", .handler = "BTL_restoreCameraView", .args = None},
    {.name = "render_2d_quad", .handler = "BTL_render2DTexturedQuad", .args = A65},
    {.name = "render_wireframe_grid", .handler = "BTL_renderWireframeGrid", .args = A66},
    {.name = "discard", .handler = "BTL_discardEFEOperand", .args = A67},
    {.name = "render_wireframe_box", .handler = "BTL_renderWireframeBox", .args = A68},
    {.name = "set_transform_to_bone_matrix", .handler = "BTL_setTransformToBoneMatrix", .args = None},
    {.name = "render_3d_quad", .handler = "BTL_render3DTexturedQuad", .args = A70},
    {.name = "multiply_vector", .handler = "BTL_multiplyVectorByScalar", .args = AScalar},
    {.name = "divide_vector", .handler = "BTL_divideVectorByScalar", .args = AScalar},
    {.name = "mask_vector", .handler = "BTL_maskVectorByScalar", .args = AScalar},
    {.name = "to_view_space", .handler = "BTL_convertToViewSpace", .args = A74},
    {.name = "select_random_target", .handler = "BTL_selectRandomTargetEntity", .args = A75},
    {.name = "get_camera_rotation", .handler = "BTL_getCameraRotation", .args = AOutVector},
    {.name = "draw_tmd_yxz", .handler = "BTL_drawTMDYXZ", .args = A3},
    {.name = "load_clut_colors", .handler = "BTL_loadClutColors", .args = A78},
    {.name = "draw_tmd_on_screen", .handler = "BTL_drawTMDScreenSpace", .args = A3},
    {.name = "queue_clut_load", .handler = "BTL_addClutLoadPrim", .args = A80},
    {.name = "get_viewport_distance", .handler = "BTL_getViewportDistance", .args = AOutInt},
    {.name = "render_radial_waves", .handler = "BTL_renderRadialWaves", .args = A82},
    {.name = "init_ribbon_points", .handler = "BTL_initializeRibbonPoints", .args = APoints},
    {.name = "tick_ribbon_points", .handler = "BTL_tickRibbonPoints", .args = APoints},
    {.name = "render_ribbon", .handler = "BTL_renderRibbonStrip", .args = A85},
    {.name = "render_ring_tube", .handler = "BTL_renderRingTube", .args = A86},
    {.name = "render_screen_overlay", .handler = "BTL_renderScreenOverlay", .args = A87},
    {.name = "face_target", .handler = "BTL_faceTargetEntity", .args = None},
    {.name = "apply_line_hit", .handler = "BTL_applyLineAttackHit", .args = A89},
    {.name = "apply_radius_hit", .handler = "BTL_applyRadiusAttackHit", .args = A90},
    {.name = "apply_box_hit", .handler = "BTL_applyBoxAttackHit", .args = A91},
    {.name = "render_screen_fade", .handler = "BTL_renderScreenFade", .args = A92},
    {.name = "disable_map_layer", .handler = "BTL_disableMapLayer", .args = None},
    {.name = "get_viewport_distance_2", .handler = "BTL_getViewportDistance2", .args = AOutInt},
    {.name = "mark_finished", .handler = "BTL_markEFEFinished", .args = None},
    {.name = "is_target_unhit", .handler = "BTL_isTargetUnhit", .args = AOutInt},
}};

} // namespace

const EfeSubOpSpec& efeSubOp(std::uint8_t code) {
    return SubOps[code];
}

} // namespace dwe::formats::effect
