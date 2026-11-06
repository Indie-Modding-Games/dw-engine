// RmlUi's geometry (ui::UiRenderer): premultiplied colours, one texture per draw, positions in pixels.
import dwe.bindless;

struct Push {
    // Pixels to clip space (render::pixelProjection) times RmlUi's transform of the element.
    float4x4 transform;
    float2 translation;
    uint texture;
    // 1 for the disc's pixel art (sharpUv), 0 for everything else, sampled linearly as is.
    uint sharp;
};

[[vk::push_constant]]
Push push;

struct VertexIn {
    [[vk::location(0)]] float2 position;
    [[vk::location(1)]] float4 color;
    [[vk::location(2)]] float2 uv;
};

struct VertexOut {
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
};

[shader("vertex")]
VertexOut vertexMain(VertexIn input) {
    VertexOut output;
    output.position = mul(push.transform, float4(input.position + push.translation, 0.0, 1.0));
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

// Pixel art at any size: each texel is a flat square, and only the screen pixel a texel edge crosses blends its two
// texels. Plain nearest sampling at a ratio that is not whole (3dp per pixel at a dp ratio of 1.5 or 2 draws a 4x
// texel 1.125 or 1.5 pixels wide) makes texels one or two pixels wide by turns, which shimmers as the image moves or
// scales; plain linear sampling blurs every edge. The linear sampler is moved to the middle of the texel except
// within half a screen pixel of its edge (fwidth: texels per screen pixel).
float2 sharpUv(uint texture, float2 uv) {
    uint width;
    uint height;
    gTextures[NonUniformResourceIndex(texture)].GetDimensions(width, height);
    const float2 size = float2(width, height);
    const float2 texel = uv * size;
    const float2 edge = floor(texel + 0.5);
    const float2 perPixel = max(fwidth(texel), float2(1.0e-5));
    return (edge + clamp((texel - edge) / perPixel, -0.5, 0.5)) / size;
}

[shader("fragment")]
float4 fragmentMain(VertexOut input) : SV_Target {
    const float2 uv = push.sharp != 0 ? sharpUv(push.texture, input.uv) : input.uv;
    return sampleTexture(push.texture, SamplerLinearClamp, uv) * input.color;
}
