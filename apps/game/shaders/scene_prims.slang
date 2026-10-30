// The field scenes' screen primitives (apps/game ScenePrimDrawer): triangles whose corners are already in clip space,
// textured and tinted, tested against the models' depth without writing it, as the original's scenes sort their
// shards, rays and flashes into the ordering table with the models.
import dwe.bindless;

struct VertexIn {
    [[vk::location(0)]] float4 position;
    [[vk::location(1)]] float2 uv;
    [[vk::location(2)]] float4 color;
    // x: texture index, y: sampler (rhi::SamplerKind).
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
    const float4 texel = sampleTexture(input.texture.x, input.texture.y, input.uv);
    // The original never draws colour 0x0000, the only transparent one.
    if (texel.a < 0.25) {
        discard;
    }
    return float4(min(texel.rgb * input.color.rgb, 1.0), texel.a * input.color.a);
}
