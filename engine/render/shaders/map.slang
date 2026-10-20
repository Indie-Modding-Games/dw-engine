// Field map drawing (render::MapRenderer): the prerendered background and its depth-layer sprites as screen-aligned
// quads placed in clip space by the map camera. The background and the sprites that follow the time of day keep
// palette indices (in red) and look their colours up in a palette texture with a row per variant: a time-of-day
// change fades between two rows. Other textures hold colours. Pixel art is sampled "sharp bilinear": nearest inside
// a texel, a one-pixel linear blend at texel edges, so any scale looks crisp without uneven pixels. Colour alpha is 0
// (never drawn), 128 (the colour's STP bit is set: mixed when the sprite is semi-transparent) or 255; a
// semi-transparent sprite is drawn twice, its plain texels opaque and its STP texels with its blend.
import dwe.bindless;

struct Push {
    // Window pixels per texel (render::MapCamera::scale).
    float pixelsPerTexel;
    // 0 shows the palette row fromRow, 1 the row toRow.
    float fade;
    // Which texels this draw keeps: 0 all drawn ones, 1 those without STP, 2 those with STP.
    uint texels;
    uint fromRow;
    uint toRow;
};

// A texture without a palette texture: its texels are colours (rhi::TextureId::InvalidIndex).
static const uint NoPalette = 0xFFFFFFFF;

[[vk::push_constant]]
Push push;

struct VertexIn {
    [[vk::location(0)]] float4 position;
    [[vk::location(1)]] float2 uv;
    // x: the texture; y: the palette texture its texels index, or NoPalette.
    [[vk::location(2)]] uint2 textures;
    // RGB multiplies the texels; A is the opacity the blend uses.
    [[vk::location(3)]] float4 color;
};

struct VertexOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation uint2 textures : TEXCOORD1;
    float4 color : COLOR0;
};

[shader("vertex")]
VertexOut vertexMain(VertexIn input) {
    VertexOut output;
    output.position = input.position;
    output.uv = input.uv;
    output.textures = input.textures;
    output.color = input.color;
    return output;
}

// One texel's colour, through the palette rows when the texture holds indices.
float4 texelAt(uint texture, uint palette, int2 texel) {
    const float4 value = gTextures[NonUniformResourceIndex(texture)].Load(int3(texel, 0));
    if (palette == NoPalette) {
        return value;
    }
    const int index = int(value.r * 255.0 + 0.5);
    const float4 from = gTextures[NonUniformResourceIndex(palette)].Load(int3(index, int(push.fromRow), 0));
    const float4 to = gTextures[NonUniformResourceIndex(palette)].Load(int3(index, int(push.toRow), 0));
    return lerp(from, to, push.fade);
}

// Indices cannot be filtered, so the four texels around uv are blended by hand, after the palette.
float4 sampleSharp(uint texture, uint palette, float2 uv) {
    uint width;
    uint height;
    gTextures[NonUniformResourceIndex(texture)].GetDimensions(width, height);
    const float2 size = float2(width, height);
    const float2 texel = uv * size;
    const float2 base = floor(texel - 0.5);
    const float2 blend = saturate(((texel - base - 0.5) - 0.5) * max(push.pixelsPerTexel, 1.0) + 0.5);
    const int2 last = int2(width, height) - 1;
    const int2 topLeft = clamp(int2(base), int2(0), last);
    const int2 bottomRight = clamp(int2(base) + 1, int2(0), last);
    const float4 top =
        lerp(texelAt(texture, palette, topLeft), texelAt(texture, palette, int2(bottomRight.x, topLeft.y)), blend.x);
    const float4 bottom = lerp(texelAt(texture, palette, int2(topLeft.x, bottomRight.y)),
                               texelAt(texture, palette, bottomRight), blend.x);
    return lerp(top, bottom, blend.y);
}

[shader("fragment")]
float4 fragmentMain(VertexOut input) : SV_Target {
    const float4 texel = sampleSharp(input.textures.x, input.textures.y, input.uv);
    // The original never draws colour 0x0000, the only transparent one.
    if (texel.a < 0.25) {
        discard;
    }
    const bool stp = texel.a < 0.75;
    if ((push.texels == 1 && stp) || (push.texels == 2 && !stp)) {
        discard;
    }
    return float4(texel.rgb * input.color.rgb, input.color.a);
}
