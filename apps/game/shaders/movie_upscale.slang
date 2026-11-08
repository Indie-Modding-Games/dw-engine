// The movies' upscaler (apps/game MovieDrawer, an engine improvement; docs/architecture/peliculas.md, «Escalado»), with
// the texture filter «upscaled»: the first of two passes (the sharpen, movie_sharpen.slang, follows), written for the
// movies' small picture.
//
// - upscaleMain (scene pass): the picture's quad at the window's size, each pixel an edge-adaptive Lanczos-2 of the
//   twelve nearest texels. The local gradient of the luma turns the kernel along an edge and stretches it there, so
//   edges stay straight and sharp instead of stair-stepped or blurred, while flat areas get the plain round kernel.
//   The result is clamped to the four nearest texels, so the kernel's negative lobes never ring.
// Values are the 0-1 sRGB-encoded ones the textures hold, as the kernels expect (they work on perceptual values).
import dwe.bindless;

struct UpscalePush {
    // Pixels to clip space (render::pixelProjection).
    float4x4 projection;
};

[[vk::push_constant]]
UpscalePush upscalePush;

struct VertexIn {
    [[vk::location(0)]] float2 position;
    [[vk::location(1)]] float2 uv;
    [[vk::location(2)]] float4 color;
    // x: texture index, y: sampler (unused: the upscaler reads texels).
    [[vk::location(3)]] uint2 texture;
};

struct VertexOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    nointerpolation uint texture : TEXCOORD1;
};

[shader("vertex")]
VertexOut vertexMain(VertexIn input) {
    VertexOut output;
    output.position = mul(upscalePush.projection, float4(input.position, 0.0, 1.0));
    output.uv = input.uv;
    output.texture = input.texture.x;
    return output;
}

// A texel of texture, its coordinate clamped to the picture.
float3 texel(uint texture, int2 at, int2 last) {
    return gTextures[NonUniformResourceIndex(texture)].Load(int3(clamp(at, int2(0, 0), last), 0)).rgb;
}

// The luma the kernel steers by: green counts twice, as in the eye.
float lumaOf(float3 color) {
    return dot(color, float3(0.5, 1.0, 0.5));
}

// How strongly the luma bends at a texel along one axis, 0 (a straight ramp or flat) to 1 (a sharp edge), and the
// signed gradient across it, from its two neighbours on that axis.
void edgeAlong(float before, float centre, float after, float weight, inout float gradient, inout float strength) {
    const float slope = after - before;
    const float steepest = max(abs(after - centre), abs(centre - before));
    const float bend = steepest > 0.0 ? saturate(abs(slope) / steepest) : 0.0;
    gradient += slope * weight;
    strength += bend * bend * weight;
}

// The kernel's weight at squared distance d2 (in texels, already stretched): a polynomial fit of Lanczos-2,
// (25/16 * (2/5 * d2 - 1)^2 - 9/16) * (window * d2 - 1)^2, where window narrows the lobe from Lanczos-2's to a softer one
// as the edge weakens.
float kernel(float d2, float window) {
    const float base = (2.0 / 5.0) * d2 - 1.0;
    const float tail = window * d2 - 1.0;
    return ((25.0 / 16.0) * base * base - (9.0 / 16.0)) * (tail * tail);
}

[shader("fragment")]
float4 fragmentMain(VertexOut input) : SV_Target {
    uint width;
    uint height;
    gTextures[NonUniformResourceIndex(input.texture)].GetDimensions(width, height);
    const int2 last = int2(int(width) - 1, int(height) - 1);
    // The pixel's position among texel centres: texel (i, j) is at (i, j).
    const float2 at = input.uv * float2(float(width), float(height)) - 0.5;
    const float2 cell = floor(at);
    const float2 f = at - cell;
    const int2 base = int2(cell);

    // The 12 texels: the 4x4 around the pixel without its corners. Named by row (b c / e f g h / i j k l / n o), as
    //       b c
    //     e f g h
    //     i j k l
    //       n o
    // with f the texel at or before the pixel and k the one after it on both axes.
    const float3 b = texel(input.texture, base + int2(0, -1), last);
    const float3 c = texel(input.texture, base + int2(1, -1), last);
    const float3 e = texel(input.texture, base + int2(-1, 0), last);
    const float3 fc = texel(input.texture, base + int2(0, 0), last);
    const float3 g = texel(input.texture, base + int2(1, 0), last);
    const float3 h = texel(input.texture, base + int2(2, 0), last);
    const float3 i = texel(input.texture, base + int2(-1, 1), last);
    const float3 j = texel(input.texture, base + int2(0, 1), last);
    const float3 k = texel(input.texture, base + int2(1, 1), last);
    const float3 l = texel(input.texture, base + int2(2, 1), last);
    const float3 n = texel(input.texture, base + int2(0, 2), last);
    const float3 o = texel(input.texture, base + int2(1, 2), last);
    const float bL = lumaOf(b), cL = lumaOf(c), eL = lumaOf(e), fL = lumaOf(fc), gL = lumaOf(g), hL = lumaOf(h);
    const float iL = lumaOf(i), jL = lumaOf(j), kL = lumaOf(k), lL = lumaOf(l), nL = lumaOf(n), oL = lumaOf(o);

    // The edge's direction and strength: each of the four inner texels' own, mixed by the pixel's bilinear weights.
    const float wf = (1.0 - f.x) * (1.0 - f.y);
    const float wg = f.x * (1.0 - f.y);
    const float wj = (1.0 - f.x) * f.y;
    const float wk = f.x * f.y;
    float2 gradient = float2(0.0, 0.0);
    float2 strength = float2(0.0, 0.0);
    edgeAlong(eL, fL, gL, wf, gradient.x, strength.x);
    edgeAlong(bL, fL, jL, wf, gradient.y, strength.y);
    edgeAlong(fL, gL, hL, wg, gradient.x, strength.x);
    edgeAlong(cL, gL, kL, wg, gradient.y, strength.y);
    edgeAlong(iL, jL, kL, wj, gradient.x, strength.x);
    edgeAlong(fL, jL, nL, wj, gradient.y, strength.y);
    edgeAlong(jL, kL, lL, wk, gradient.x, strength.x);
    edgeAlong(gL, kL, oL, wk, gradient.y, strength.y);

    // The gradient's unit direction (any, on a flat area), and how much of an edge there is, 0 to 1.
    const float gradientLength2 = dot(gradient, gradient);
    const float2 direction = gradientLength2 < (1.0 / 32768.0) ? float2(1.0, 0.0) : gradient * rsqrt(gradientLength2);
    float edge = (strength.x + strength.y) * 0.5;
    edge *= edge;
    // Along a diagonal the texel grid is sparser: stretch more there (1 on an axis, sqrt(2) on a diagonal).
    const float diagonal = 1.0 / max(abs(direction.x), abs(direction.y));
    // Across the edge the kernel narrows on diagonals (where the grid is sparser); along it, it widens with the
    // edge's strength, so the edge's texels average along it and not across it.
    const float2 stretch = float2(1.0 + (diagonal - 1.0) * edge, 1.0 - 0.5 * edge);
    // The kernel's window: Lanczos-2's lobe on a strong edge, a softer, narrower one on flat areas.
    const float window = 0.5 - 0.29 * edge;
    const float reach = 1.0 / window;

    float3 sum = float3(0.0, 0.0, 0.0);
    float total = 0.0;
    const float2 offsets[12] = {
        float2(0.0, -1.0), float2(1.0, -1.0), float2(-1.0, 0.0), float2(0.0, 0.0), float2(1.0, 0.0), float2(2.0, 0.0),
        float2(-1.0, 1.0), float2(0.0, 1.0), float2(1.0, 1.0), float2(2.0, 1.0), float2(0.0, 2.0), float2(1.0, 2.0),
    };
    const float3 colors[12] = { b, c, e, fc, g, h, i, j, k, l, n, o };
    [unroll]
    for (int t = 0; t < 12; ++t) {
        const float2 v = offsets[t] - f;
        // Into the edge's frame: x across it (along the gradient), y along it.
        const float2 rotated = float2(dot(v, direction), dot(v, float2(-direction.y, direction.x)));
        const float2 scaled = rotated * stretch;
        const float d2 = min(dot(scaled, scaled), reach);
        const float weight = kernel(d2, window);
        sum += colors[t] * weight;
        total += weight;
    }
    float3 color = sum / total;
    // No ringing: the result stays within the four nearest texels.
    const float3 low = min(min(fc, g), min(j, k));
    const float3 high = max(max(fc, g), max(j, k));
    color = clamp(color, low, high);
    return float4(color, 1.0);
}
