// The render foundation's test scene (apps/game TestScene): textured, vertex-colored meshes whose matrices are read
// from the upload ring through a device address, as skinned models read their joints.
import dwe.bindless;

struct Push {
    // World-view-projection matrices for this frame's draws.
    float4x4* transforms;
    uint transform;
    uint texture;
    uint sampler;
};

[[vk::push_constant]]
Push push;

struct VertexIn {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float2 uv;
    [[vk::location(2)]] float4 color;
};

struct VertexOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

[shader("vertex")]
VertexOut vertexMain(VertexIn input) {
    VertexOut output;
    output.position = mul(push.transforms[push.transform], float4(input.position, 1.0));
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOut input) : SV_Target {
    return sampleTexture(push.texture, push.sampler, input.uv) * input.color;
}
