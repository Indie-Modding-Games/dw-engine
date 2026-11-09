// The field's light and color pass (render::SceneGrade, docs/architecture/render.md, «Paso de luz y color»): draws
// the scene the scene pass left in a texture into the window, with the time of day's tint, the map's point lights,
// vibrance, a gentle contrast curve and a soft bloom on the brightest areas. One full-screen triangle; every value is
// in the 0-1 sRGB-encoded space the scene was drawn in.
import dwe.bindless;

struct Light {
    // Window pixels: the centre and the radius where the light ends.
    float2 centre;
    float radius;
    float strength;
    // The light's color (w unused).
    float4 color;
};

struct Push {
    Light* lights;
    uint2 padding;
    // Multiplies the scene (the time of day); w unused.
    float4 tint;
    // x vibrance, y contrast, z bloom, w the bloom's threshold.
    float4 grade;
    // The window's last pixel (its size less one), and the bloom's reach in pixels.
    int2 lastPixel;
    int bloomReach;
    uint scene;
    uint lightCount;
};

[[vk::push_constant]]
Push push;

struct VertexOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
};

// One triangle that covers the window: (0, 0), (2, 0), (0, 2) in uv.
[shader("vertex")]
VertexOut vertexMain(uint vertex : SV_VulkanVertexID) {
    VertexOut output;
    const float2 uv = float2(float((vertex << 1) & 2), float(vertex & 2));
    output.uv = uv;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

// The scene's texel at a window pixel, unfiltered (the scene is the window's size), clamped to the window. The
// scene's index is the same for every pixel.
float3 sceneAt(int2 pixel) {
    return gTextures[push.scene].Load(int3(clamp(pixel, int2(0), push.lastPixel), 0)).rgb;
}

// What of a sample is above the bloom's threshold.
float3 brightPart(float3 color) {
    return max(color - push.grade.w, 0.0);
}

[shader("fragment")]
float4 fragmentMain(VertexOut input) : SV_Target {
    const float2 pixel = input.position.xy;
    const int2 at = int2(pixel);
    float3 color = sceneAt(at) * push.tint.rgb;

    // Point lights: each brightens what it falls on (so a dark wall stays a wall) and adds a faint glow of its own.
    float3 light = float3(0.0);
    for (uint index = 0; index < push.lightCount; ++index) {
        const Light source = push.lights[index];
        const float reach = length(pixel - source.centre) / max(source.radius, 1.0);
        const float falloff = saturate(1.0 - reach * reach);
        light += source.color.rgb * (source.strength * falloff * falloff);
    }
    color += color * light * 1.4 + light * 0.2;

    // Vibrance: muted colours gain more saturation than vivid ones, greys and skin tones barely move.
    const float vibrance = push.grade.x;
    if (vibrance != 0.0) {
        const float luma = dot(color, float3(0.299, 0.587, 0.114));
        const float saturation = max(color.r, max(color.g, color.b)) - min(color.r, min(color.g, color.b));
        color = lerp(float3(luma), color, 1.0 + vibrance * (1.0 - saturation));
    }

    // Contrast: towards an S curve that keeps black and white where they are.
    color = saturate(color);
    color = lerp(color, color * color * (3.0 - 2.0 * color), push.grade.y);

    // Bloom: the bright part of four texels around the pixel, two near and two far on the other diagonal; unfiltered
    // taps keep it cheap on a software GPU, and at a few pixels' reach the glow hides that they are single texels.
    const float bloom = push.grade.z;
    if (bloom > 0.0) {
        const int near = push.bloomReach;
        const int far = (push.bloomReach * 9) / 4;
        float3 glow = brightPart(sceneAt(at + int2(near, near)));
        glow += brightPart(sceneAt(at - int2(near, near)));
        glow += brightPart(sceneAt(at + int2(far, -far)));
        glow += brightPart(sceneAt(at + int2(-far, far)));
        color += glow * (bloom / 3.2);
    }
    return float4(saturate(color), 1.0);
}
