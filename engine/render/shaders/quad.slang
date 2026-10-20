// Textured, tinted quads in pixel coordinates (render::QuadBatch). Each vertex names its texture and sampler, so a
// whole batch is one draw.
import dwe.bindless;

struct Push {
    // Pixels to clip space (render::pixelProjection).
    float4x4 projection;
};

[[vk::push_constant]]
Push push;

struct VertexIn {
    [[vk::location(0)]] float2 position;
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
    output.position = mul(push.projection, float4(input.position, 0.0, 1.0));
    output.uv = input.uv;
    output.color = input.color;
    output.texture = input.texture;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOut input) : SV_Target {
    return sampleTexture(input.texture.x, input.texture.y, input.uv) * input.color;
}
