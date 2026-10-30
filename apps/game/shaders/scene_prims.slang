// The field scenes' screen primitives (apps/game ScenePrimDrawer): triangles whose corners are already in clip space,
// textured and tinted, tested against the models' depth without writing it, as the original's scenes sort their
// shards, rays and flashes into the ordering table with the models.
import dwe.bindless;

struct VertexIn {
    [[vk::location(0)]] float4 position;
    [[vk::location(1)]] float2 uv;
    [[vk::location(2)]] float4 color;
    // x: texture index, y: sampler (rhi::SamplerKind) in the low byte and the texel filter over it:
    // 0 every texel, 1 only those without STP (alpha 255, drawn opaque), 2 only those with it (alpha
    // 128 in an image that keeps STP; the original mixes only these).
    [[vk::location(3)]] uint2 texture;
};

struct VertexOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    nointerpolation uint2 texture : TEXCOORD1;
};

[shader("vertex")]
VertexOut vertexMain(VertexIn input) {
    VertexOut output;
    output.position = input.position;
    output.uv = input.uv;
    output.color = input.color;
    output.texture = input.texture;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOut input) : SV_Target {
    const float4 texel = sampleTexture(input.texture.x, input.texture.y & 0xFFu, input.uv);
    // The original never draws colour 0x0000, the only transparent one.
    if (texel.a < 0.25) {
        discard;
    }
    const uint filter = input.texture.y >> 8;
    const bool semiTransparent = texel.a < 0.75;
    if ((filter == 1u && semiTransparent) || (filter == 2u && !semiTransparent)) {
        discard;
    }
    // A filtered texel counts whole: the pipeline's blend is the original's mode for it.
    const float alpha = filter != 0u ? 1.0 : texel.a;
    return float4(min(texel.rgb * input.color.rgb, 1.0), alpha * input.color.a);
}
