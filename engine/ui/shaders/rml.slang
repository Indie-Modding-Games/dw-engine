// RmlUi's geometry (ui::UiRenderer): premultiplied colours, one texture per draw, positions in pixels.
import dwe.bindless;

struct Push {
    // Pixels to clip space (render::pixelProjection) times RmlUi's transform of the element.
    float4x4 transform;
    float2 translation;
    uint texture;
    uint padding;
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

[shader("fragment")]
float4 fragmentMain(VertexOut input) : SV_Target {
    return sampleTexture(push.texture, SamplerLinearClamp, input.uv) * input.color;
}
