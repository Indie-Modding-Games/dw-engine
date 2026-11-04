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
    // The scenery's grade (render::MapGrade, the seasons): 0 leaves the colours as they are. Greens
    // turn toward foliageHue (0 to 1 of the circle) by foliage, then the saturation, the gain and
    // the lift.
    uint graded;
    float foliageHue;
    float foliage;
    float saturation;
    float gainRed;
    float gainGreen;
    float gainBlue;
    float lift;
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

float3 rgbToHsv(float3 c) {
    const float high = max(c.r, max(c.g, c.b));
    const float low = min(c.r, min(c.g, c.b));
    const float range = high - low;
    float hue = 0.0;
    if (range > 1e-5) {
        if (high == c.r) {
            hue = (c.g - c.b) / range;
        } else if (high == c.g) {
            hue = 2.0 + (c.b - c.r) / range;
        } else {
            hue = 4.0 + (c.r - c.g) / range;
        }
        hue = frac(hue / 6.0 + 1.0);
    }
    return float3(hue, high > 0.0 ? range / high : 0.0, high);
}

float3 hsvToRgb(float3 c) {
    const float3 k = saturate(abs(frac(c.x + float3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0) - 1.0);
    return c.z * lerp(float3(1.0, 1.0, 1.0), k, c.y);
}

// The season's grade of a scenery colour.
float3 graded(float3 colour) {
    if (push.foliage > 0.0) {
        float3 hsv = rgbToHsv(colour);
        // Foliage: yellow-greens to blue-greens (about 60 to 170 degrees) with some colour in them.
        const float green = smoothstep(0.15, 0.23, hsv.x) * (1.0 - smoothstep(0.43, 0.5, hsv.x)) *
                            smoothstep(0.08, 0.25, hsv.y) * smoothstep(0.04, 0.12, hsv.z);
        // Darker greens (the trees' leaves) turn more than the bright ones (the grass).
        const float turn = saturate(push.foliage * green * lerp(1.35, 0.7, smoothstep(0.25, 0.6, hsv.z)));
        hsv.x = lerp(hsv.x, push.foliageHue, turn);
        // Turned leaves are a little darker (brown) than the green they were.
        hsv.z *= 1.0 - 0.12 * turn * step(push.foliageHue, 0.15);
        colour = hsvToRgb(hsv);
    }
    const float luma = dot(colour, float3(0.299, 0.587, 0.114));
    colour = lerp(float3(luma, luma, luma), colour, push.saturation);
    return saturate(colour * float3(push.gainRed, push.gainGreen, push.gainBlue) + push.lift);
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
    const float3 colour = push.graded != 0 ? graded(texel.rgb) : texel.rgb;
    return float4(colour * input.color.rgb, input.color.a);
}
