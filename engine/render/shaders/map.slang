// Field map drawing (render::MapRenderer): the prerendered background and its depth-layer sprites as screen-aligned
// quads placed in clip space by the map camera. The background and the sprites that follow the time of day keep
// palette indices (in red) and look their colours up in a palette texture with a row per variant: a time-of-day
// change fades between two rows. Other textures hold colours. Pixel art is sampled "sharp bilinear": nearest inside
// a texel, a one-pixel linear blend at texel edges, so any scale looks crisp without uneven pixels. Colour alpha is 0
// (never drawn), 128 (the colour's STP bit is set: mixed when the sprite is semi-transparent) or 255; a
// semi-transparent sprite is drawn twice, its plain texels opaque and its STP texels with its blend.
//
// HD scenery (an engine improvement, docs/architecture/remaster-pipeline.md, «Alta definición en el motor») draws the
// background and the depth layers through sampleSmooth instead: an edge-adaptive filter at window resolution that
// reads the colours after the palette fade, so every palette variant and the fade work as before. Which texels are
// drawn, and their STP class, still come from the nearest texel: the scenery's shapes and what covers what do not move.
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
    // HD scenery: the background filtered at the window's pixels by its background pass, for SamplingScreen and
    // SamplingCutoutScreen, and where it has the window's top-left pixel: it wraps around as the camera scrolls
    // (render::MapRenderer::hdSceneryScene).
    uint screen;
    uint screenShiftX;
    uint screenShiftY;
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
    // x: how the texels are sampled (SamplingSharp, SamplingSmooth, SamplingCutout); y: for SamplingCutout, the
    // texture the colours come from (the background's colours with the palette shown now: no palette texture).
    [[vk::location(4)]] uint2 sampling;
    // For SamplingCutout, where the colours are read, as uv of sampling.y.
    [[vk::location(5)]] float2 sourceUv;
    // The texels the smooth filter may read (first x, first y, last x, last y) of the texture the colours come from:
    // a sprite's rectangle in its sheet, so neighbouring sprites do not bleed in.
    [[vk::location(6)]] float4 rect;
};

struct VertexOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation uint2 textures : TEXCOORD1;
    float4 color : COLOR0;
    nointerpolation uint2 sampling : TEXCOORD2;
    float2 sourceUv : TEXCOORD3;
    nointerpolation float4 rect : TEXCOORD4;
};

// The original's pixels, sampled sharp bilinear with push.pixelsPerTexel.
static const uint SamplingSharp = 0;
// HD scenery: the texture's own texels through sampleSmooth.
static const uint SamplingSmooth = 1;
// HD scenery for a sprite cut out of the background: which texels are drawn and their class come from the sprite, the
// colours from the background under it through sampleSmooth, so the sprite matches the background pixel for pixel.
static const uint SamplingCutout = 2;
// HD scenery's background pass: the background's colours through sampleSmooth into a target the window's size, where
// the window shows something not filtered yet; its alpha the texels' class.
static const uint SamplingPrefilter = 3;
// The background, as that pass drew it at this pixel (push.screen): one load instead of the filter.
static const uint SamplingScreen = 4;
// A cut-out, its colours as that pass drew them at this pixel.
static const uint SamplingCutoutScreen = 5;

[shader("vertex")]
VertexOut vertexMain(VertexIn input) {
    VertexOut output;
    output.position = input.position;
    output.uv = input.uv;
    output.textures = input.textures;
    output.color = input.color;
    output.sampling = input.sampling;
    output.sourceUv = input.sourceUv;
    output.rect = input.rect;
    return output;
}

// One texel's colour, through the palette rows when the texture holds indices.
float4 texelAt(uint texture, uint palette, int2 texel) {
    const float4 value = gTextures[NonUniformResourceIndex(texture)].Load(int3(texel, 0));
    if (palette == NoPalette) {
        return value;
    }
    const int index = int(value.r * 255.0 + 0.5);
    // Out of a fade one row is enough (the same colours: lerp gives either end exactly).
    if (push.fade >= 1.0 || push.fromRow == push.toRow) {
        return gTextures[NonUniformResourceIndex(palette)].Load(int3(index, int(push.toRow), 0));
    }
    const float4 from = gTextures[NonUniformResourceIndex(palette)].Load(int3(index, int(push.fromRow), 0));
    if (push.fade <= 0.0) {
        return from;
    }
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

// The smooth filter's taps: the 4 x 4 texels around the sample point without its corners, from the texel up and
// to the left of it (0, 0). The middle four, (0, 0), (1, 0), (0, 1) and (1, 1), are taps 3, 4, 7 and 8.
static const int2 SmoothTaps[12] = {
    int2(0, -1), int2(1, -1),
    int2(-1, 0), int2(0, 0), int2(1, 0), int2(2, 0),
    int2(-1, 1), int2(0, 1), int2(1, 1), int2(2, 1),
    int2(0, 2), int2(1, 2),
};

// How much an edge narrows the kernel across it (sharper edges) and widens it along it (smooth lines, no stairs).
// Stronger values draw lines cleaner but turn foliage and rock into brush strokes.
static const float EdgeSqueeze = 1.0;
static const float EdgeStretch = 1.2;
// The local contrast from which a gradient starts to count as an edge, and the one from which it fully does: the luma
// change over two texels relative to the local brightness plus ContrastFloor, so the night's darker palettes keep
// their edges as the day's do.
static const float EdgeLow = 0.05;
static const float EdgeHigh = 0.4;
static const float ContrastFloor = 0.2;
// The kernel's negative lobe: 1 makes it pass through every texel's own colour at its centre (detail kept, sharp);
// smaller values blend the neighbours in. Flat areas take the smaller one, which melts the original's dithering into
// smooth shades; edges and contrasted detail the sharp one.
static const float LobeFlat = 0.75;
static const float LobeEdge = 1.0;

// Texels the filter's sample point is moved right and down by (sampleSmooth).
static const float SmoothBias = 1.0 / 512.0;

float luma(float3 colour) {
    return dot(colour, float3(0.299, 0.587, 0.114));
}

// The kernel's weight at a squared distance of x texels (0 to 4): a bell to 0 at 2 texels, with a negative lobe.
float smoothKernel(float x, float lobe) {
    const float window = 1.0 - (0.25 * x);
    return window * window * (1.0 - (lobe * x));
}

// The texels sampleSmooth reads.
interface ITexels {
    uint2 extent();
    float4 at(int2 texel);
};

// Any texture of a draw, through its palette: draws with quads of several textures index the table non-uniformly.
struct DrawTexels : ITexels {
    uint texture;
    uint palette;

    uint2 extent() {
        uint width;
        uint height;
        gTextures[NonUniformResourceIndex(texture)].GetDimensions(width, height);
        return uint2(width, height);
    }

    float4 at(int2 texel) {
        return texelAt(texture, palette, texel);
    }
};

// The colours of the one texture a whole draw reads (the background pass draws a single quad): a uniform index,
// cheaper on software rasterizers than the non-uniform one.
struct UniformColours : ITexels {
    uint texture;

    uint2 extent() {
        uint width;
        uint height;
        gTextures[texture].GetDimensions(width, height);
        return uint2(width, height);
    }

    float4 at(int2 texel) {
        return gTextures[texture].Load(int3(texel, 0));
    }
};

// HD scenery's filter: an edge-adaptive kernel over 12 texels, written for this engine, read through `texels` (ITexels).
// The background comes as its colours with the palette shown now (render::MapRenderer resolves them when the palette
// changes), and so do the sprites that follow the time of day, so each tap is one load; other sprites may still index
// a palette. The four middle texels' gradients (on luma) give the local edge direction and how strong and coherent it is (a structure tensor); the kernel
// is then a bell squeezed across the edge and stretched along it, so edges come out as clean smooth lines at any
// window size, while flat and noisy areas get a plain smooth bell. The result is clamped to the colours of the four
// middle texels (no halos or ringing). Transparent texels (alpha class 0) carry no weight, so no dark fringe comes in
// from them, and only texels of the nearest one's class (opaque or STP) mix; alpha is the nearest texel's class, so
// what is drawn and what is STP keep the original's pixels. Colours are the ones after the palette fade, which is
// therefore the original's.
float4 sampleSmooth<T : ITexels>(T texels, float2 uv, float4 rect) {
    // Pixel centres often fall exactly on a texel's centre (scale 3 puts every third one there), where the 12 taps and
    // the clamp switch to the next texels: a hair to the right and down keeps them from switching with the rounding of
    // how the quad was drawn, so the background pass's strips match one another (MapRenderer::hdSceneryScene).
    const float2 position = (uv * float2(texels.extent())) - 0.5 + SmoothBias;
    const float2 base = floor(position);
    const float2 f = position - base;
    const int2 first = int2(rect.xy);
    const int2 last = int2(rect.zw);

    float4 values[12];
    [unroll]
    for (int tap = 0; tap < 12; ++tap) {
        values[tap] = texels.at(clamp(int2(base) + SmoothTaps[tap], first, last));
    }
    const float4 nearest = values[(f.x < 0.5 ? 3 : 4) + (f.y < 0.5 ? 0 : 4)];
    const bool nearestStp = nearest.a < 0.75;

    float3 colours[12];
    float lumas[12];
    bool drawn[12];
    float middleLuma = 0.0;
    float middleDrawn = 0.0;
    [unroll]
    for (int tap = 0; tap < 12; ++tap) {
        const float4 value = values[tap];
        colours[tap] = value.rgb;
        lumas[tap] = luma(value.rgb);
        // Only texels of the nearest one's class mix: STP texels are blended over what is behind them in a pass of
        // their own, so their colours (often dark) would stain opaque pixels, and opaque ones STP pixels.
        drawn[tap] = value.a >= 0.25 && ((value.a < 0.75) == nearestStp);
        if ((tap == 3 || tap == 4 || tap == 7 || tap == 8) && drawn[tap]) {
            middleLuma += lumas[tap];
            middleDrawn += 1.0;
        }
    }
    if (nearest.a < 0.25 || middleDrawn == 0.0) {
        return nearest;
    }
    // Transparent texels make no edge: they read as the middle's mean.
    middleLuma /= middleDrawn;
    [unroll]
    for (int tap = 0; tap < 12; ++tap) {
        lumas[tap] = drawn[tap] ? lumas[tap] : middleLuma;
    }

    // Central differences at the four middle texels, mixed bilinearly into a structure tensor.
    const float2 gradients[4] = {
        float2(lumas[4] - lumas[2], lumas[7] - lumas[0]),
        float2(lumas[5] - lumas[3], lumas[8] - lumas[1]),
        float2(lumas[8] - lumas[6], lumas[10] - lumas[3]),
        float2(lumas[9] - lumas[7], lumas[11] - lumas[4]),
    };
    const float weights[4] = {(1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y};
    float3 tensor = float3(0.0); // xx, xy, yy
    [unroll]
    for (int corner = 0; corner < 4; ++corner) {
        const float2 g = gradients[corner];
        tensor += weights[corner] * float3(g.x * g.x, g.x * g.y, g.y * g.y);
    }
    const float spread = tensor.x - tensor.z;
    const float root = sqrt((spread * spread) + (4.0 * tensor.y * tensor.y));
    const float energy = tensor.x + tensor.z;
    // The across-the-edge direction (the dominant gradient's), from its doubled angle (cos 2a, sin 2a).
    float2 across = float2(1.0, 0.0);
    if (root > 1.0e-6) {
        const float2 doubled = float2(spread, 2.0 * tensor.y) / root;
        across = float2(sqrt(saturate(0.5 + (0.5 * doubled.x))),
                        (doubled.y < 0.0 ? -1.0 : 1.0) * sqrt(saturate(0.5 - (0.5 * doubled.x))));
    }
    const float2 along = float2(-across.y, across.x);
    const float coherence = root / (energy + 1.0e-6);
    const float strength = sqrt(0.5 * (energy + root));
    const float contrast = smoothstep(EdgeLow, EdgeHigh, strength / (middleLuma + ContrastFloor));
    const float edge = coherence * coherence * contrast;
    const float lobe = lerp(LobeFlat, LobeEdge, contrast);
    const float squeeze = 1.0 + (EdgeSqueeze * edge);
    const float stretch = 1.0 / (1.0 + (EdgeStretch * edge));

    float3 sum = float3(0.0);
    float total = 0.0;
    float3 low = float3(1.0e6);
    float3 high = float3(-1.0e6);
    [unroll]
    for (int tap = 0; tap < 12; ++tap) {
        if (!drawn[tap]) {
            continue;
        }
        const float2 offset = float2(SmoothTaps[tap]) - f;
        const float u = dot(offset, across) * squeeze;
        const float v = dot(offset, along) * stretch;
        const float x = min((u * u) + (v * v), 4.0);
        const float weight = smoothKernel(x, lobe);
        sum += weight * colours[tap];
        total += weight;
        if (tap == 3 || tap == 4 || tap == 7 || tap == 8) {
            low = min(low, colours[tap]);
            high = max(high, colours[tap]);
        }
    }
    if (total < 1.0e-3) {
        return nearest;
    }
    return float4(clamp(sum / total, low, high), nearest.a);
}

// HD scenery's filtered background at a window pixel.
float4 screenAt(float2 position) {
    uint width;
    uint height;
    gTextures[push.screen].GetDimensions(width, height);
    uint2 texel = uint2(position) + uint2(push.screenShiftX, push.screenShiftY);
    texel.x -= texel.x >= width ? width : 0;
    texel.y -= texel.y >= height ? height : 0;
    return gTextures[push.screen].Load(int3(int2(texel), 0));
}

[shader("fragment")]
float4 fragmentMain(VertexOut input) : SV_Target {
    if (input.sampling.x == SamplingPrefilter) {
        // The pass draws over what the target held: nothing past the background's edges.
        if (any(input.uv < 0.0) || any(input.uv > 1.0)) {
            return float4(0.0);
        }
        const UniformColours background = { input.textures.x };
        return sampleSmooth(background, input.uv, input.rect);
    }
    float4 texel;
    if (input.sampling.x == SamplingSmooth) {
        const DrawTexels sprite = { input.textures.x, input.textures.y };
        texel = sampleSmooth(sprite, input.uv, input.rect);
    } else if (input.sampling.x == SamplingScreen) {
        texel = screenAt(input.position.xy);
    } else if (input.sampling.x == SamplingCutout || input.sampling.x == SamplingCutoutScreen) {
        // The sprite's nearest texel says whether it is drawn and its class; the background under it its colour.
        uint width;
        uint height;
        gTextures[NonUniformResourceIndex(input.textures.x)].GetDimensions(width, height);
        const int2 spriteTexel = clamp(int2(floor(input.uv * float2(width, height))), int2(0), int2(width, height) - 1);
        const float4 mask = texelAt(input.textures.x, input.textures.y, spriteTexel);
        if (mask.a < 0.25) {
            discard;
        }
        float3 colour;
        if (input.sampling.x == SamplingCutoutScreen) {
            colour = screenAt(input.position.xy).rgb;
        } else {
            const DrawTexels background = { input.sampling.y, NoPalette };
            colour = sampleSmooth(background, input.sourceUv, input.rect).rgb;
        }
        texel = float4(colour, mask.a);
    } else {
        texel = sampleSharp(input.textures.x, input.textures.y, input.uv);
    }
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
