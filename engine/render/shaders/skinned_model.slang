// Skinned models (render::ModelRenderer): each vertex follows one or more joints, whose matrices (already multiplied
// by the instance's world transform) come from the upload ring through a device address. Lit like the original:
// ambient plus three directional lights, where 1.0 leaves the texel as it is and the light may brighten it up to 2x.
import dwe.bindless;

struct Light {
    // The direction the light travels, in world space, and its color (w unused).
    float4 direction;
    float4 color;
};

struct Frame {
    float4x4 viewProjection;
    Light lights[3];
    float4 ambient;
};

// Material flags, as render::ModelRenderer sets them.
static const uint FlagUnlit = 1;
static const uint FlagMask = 2;

struct Push {
    // One matrix per joint: world * joint * inverse bind.
    float4x4* joints;
    Frame* frame;
    float4 baseColor;
    uint texture;
    uint flags;
    float alphaCutoff;
    // The alpha the blend equation uses (0.5 average, 1 add or subtract, 0.25 add a quarter); 1 when opaque.
    float blendAlpha;
};

[[vk::push_constant]]
Push push;

struct VertexIn {
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float3 normal;
    [[vk::location(2)]] float2 uv;
    [[vk::location(3)]] float4 color;
    [[vk::location(4)]] uint4 joints;
    [[vk::location(5)]] float4 weights;
};

struct VertexOut {
    float4 position : SV_Position;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

[shader("vertex")]
VertexOut vertexMain(VertexIn input) {
    float4x4 skin = push.joints[input.joints.x] * input.weights.x + push.joints[input.joints.y] * input.weights.y +
                    push.joints[input.joints.z] * input.weights.z + push.joints[input.joints.w] * input.weights.w;
    VertexOut output;
    float4 world = mul(skin, float4(input.position, 1.0));
    output.position = mul(push.frame->viewProjection, world);
    output.normal = mul(skin, float4(input.normal, 0.0)).xyz;
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VertexOut input) : SV_Target {
    float4 texel = sampleTexture(push.texture, SamplerNearestClamp, input.uv);
    if ((push.flags & FlagMask) != 0 && texel.a < push.alphaCutoff) {
        discard;
    }
    float3 color = texel.rgb * input.color.rgb * push.baseColor.rgb;
    if ((push.flags & FlagUnlit) == 0) {
        // A degenerate normal (a joint scaled to zero) gets only the ambient light instead of NaN.
        float normalLength = length(input.normal);
        float3 normal = normalLength > 0.0 ? input.normal / normalLength : float3(0.0);
        float3 light = push.frame->ambient.rgb;
        for (uint i = 0; i < 3; ++i) {
            light += max(dot(normal, -push.frame->lights[i].direction.xyz), 0.0) * push.frame->lights[i].color.rgb;
        }
        color *= clamp(light, 0.0, 2.0);
    }
    return float4(min(color, 1.0), push.blendAlpha);
}
