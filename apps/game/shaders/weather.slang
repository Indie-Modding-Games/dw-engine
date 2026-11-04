// The field's weather (apps/game WeatherDrawer, an engine improvement): screen quads over the scene and under the UI,
// shaded by kind: a flat tint, a rain streak that fades toward its tail, a soft snowflake, a splash ring and the
// sky's veil (a flat tint under the moving cloud shadows, cut out of a tileable noise picture), in one pass.
import dwe.bindless;

struct VertexIn {
    // Already in clip space (w 1).
    [[vk::location(0)]] float4 position;
    [[vk::location(1)]] float2 uv;
    [[vk::location(2)]] float4 color;
    // x: the cloud picture's texture index, y: the shape (WeatherShape).
    [[vk::location(3)]] uint2 info;
    // The sky's cloud shadows: colour and strength.
    [[vk::location(4)]] float4 shadow;
};

struct VertexOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
    nointerpolation uint2 info : TEXCOORD1;
    nointerpolation float4 shadow : TEXCOORD2;
};

[shader("vertex")]
VertexOut vertexMain(VertexIn input) {
    VertexOut output;
    output.position = input.position;
    output.uv = input.uv;
    output.color = input.color;
    output.info = input.info;
    output.shadow = input.shadow;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOut input) : SV_Target {
    float alpha = 1.0;
    switch (input.info.y) {
    case 1u: {
        // Streak: v from 0 (tail) to 1 (head), u across.
        const float across = 1.0 - abs(input.uv.x * 2.0 - 1.0);
        alpha = input.uv.y * input.uv.y * saturate(across * 2.0);
        break;
    }
    case 2u: {
        // Soft dot.
        const float r = length(input.uv * 2.0 - 1.0);
        alpha = 1.0 - smoothstep(0.35, 1.0, r);
        break;
    }
    case 3u: {
        // The sky: the shadow (the noise's red channel, cut into soft patches) over the tint, as one
        // blend, so the map is covered once.
        float shade = 0.0;
        if (input.shadow.a > 0.0) {
            const float n = sampleTexture(input.info.x, SamplerLinearRepeat, input.uv).r;
            shade = input.shadow.a * smoothstep(0.45, 0.75, n);
        }
        const float a = 1.0 - (1.0 - input.color.a) * (1.0 - shade);
        if (a <= 0.002) {
            discard;
        }
        const float3 lit = input.shadow.rgb * shade + input.color.rgb * input.color.a * (1.0 - shade);
        return float4(lit / a, a);
    }
    case 4u: {
        // Splash ring.
        const float r = length(input.uv * 2.0 - 1.0);
        alpha = saturate(1.0 - abs(r - 0.7) / 0.3) * step(r, 1.0);
        break;
    }
    default:
        break;
    }
    const float a = alpha * input.color.a;
    if (a <= 0.002) {
        discard;
    }
    return float4(input.color.rgb, a);
}
