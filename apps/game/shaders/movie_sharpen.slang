// The movies' sharpen pass (apps/game MovieDrawer, an engine improvement; docs/architecture/peliculas.md, «Escalado»),
// after movie_upscale.slang: a contrast-adaptive five-tap sharpen of the upscaled frame, over the window, whose
// strength is limited per pixel so that no channel leaves 0..1, and eased off where the neighbourhood is noise rather
// than detail. Values are the 0-1 sRGB-encoded ones the scene holds.
import dwe.bindless;

struct Push {
    // The scene texture and its last pixel.
    int2 lastPixel;
    uint scene;
    // 0 (none) to 1 (the most the limits allow).
    float amount;
};

[[vk::push_constant]]
Push push;

struct VertexOut {
    float4 position : SV_Position;
};

// One triangle that covers the window.
[shader("vertex")]
VertexOut vertexMain(uint vertex : SV_VulkanVertexID) {
    VertexOut output;
    const float2 uv = float2(float((vertex << 1) & 2), float(vertex & 2));
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

// The luma the noise test reads: green counts twice, as in the eye.
float lumaOf(float3 color) {
    return dot(color, float3(0.5, 1.0, 0.5));
}

float3 sceneAt(int2 pixel) {
    return gTextures[push.scene].Load(int3(clamp(pixel, int2(0, 0), push.lastPixel), 0)).rgb;
}

[shader("fragment")]
float4 fragmentMain(VertexOut input) : SV_Target {
    const int2 pixel = int2(input.position.xy);
    const float3 centre = sceneAt(pixel);
    const float3 up = sceneAt(pixel + int2(0, -1));
    const float3 left = sceneAt(pixel + int2(-1, 0));
    const float3 right = sceneAt(pixel + int2(1, 0));
    const float3 down = sceneAt(pixel + int2(0, 1));
    const float3 low = min(min(up, down), min(left, right));
    const float3 high = max(max(up, down), max(left, right));
    // The filter is (centre + w * (up + down + left + right)) / (1 + 4w) with w <= 0. Each channel stays within
    // 0..1 while w >= -low / (4 high) (it cannot go below 0) and w >= (high - 1) / (4 - 4 low) (it cannot go above
    // 1): the strongest w both allow, per channel, and the weakest of the three.
    const float3 floorLimit = -low / max(4.0 * high, 1.0 / 256.0);
    const float3 ceilingLimit = (high - 1.0) / max(4.0 - 4.0 * low, 1.0 / 256.0);
    const float3 limits = max(floorLimit, ceilingLimit);
    float w = max(max(limits.r, limits.g), limits.b);
    // Never past -3/16, where the filter starts to look like an edge of its own.
    w = max(w, -3.0 / 16.0);
    // Noise (the centre far from its neighbours, which agree) gets less: grain is not detail.
    const float lumaUp = lumaOf(up), lumaDown = lumaOf(down), lumaLeft = lumaOf(left), lumaRight = lumaOf(right);
    const float lumaCentre = lumaOf(centre);
    const float spread = max(max(lumaUp, lumaDown), max(max(lumaLeft, lumaRight), lumaCentre)) -
                         min(min(lumaUp, lumaDown), min(min(lumaLeft, lumaRight), lumaCentre));
    const float peak = abs(0.25 * (lumaUp + lumaDown + lumaLeft + lumaRight) - lumaCentre);
    const float noise = spread > 0.0 ? saturate(peak / spread) : 0.0;
    w *= push.amount * (1.0 - 0.5 * noise);
    const float3 color = (centre + w * (up + down + left + right)) / (1.0 + 4.0 * w);
    return float4(saturate(color), 1.0);
}
