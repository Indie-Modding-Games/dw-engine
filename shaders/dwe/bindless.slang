// The global descriptor set every pipeline shares (engine/rhi BindlessTable, docs/architecture/render.md): every
// texture the renderer knows sits in one array, and a texture is named by its index, passed in push constants or in
// vertex data. The samplers are fixed and named by the Sampler constants below.
module bindless;

[[vk::binding(0, 0)]]
public Texture2D gTextures[];

[[vk::binding(1, 0)]]
public SamplerState gSamplers[4];

// The order of rhi::SamplerKind.
public static const uint SamplerNearestClamp = 0;
public static const uint SamplerNearestRepeat = 1;
public static const uint SamplerLinearClamp = 2;
public static const uint SamplerLinearRepeat = 3;

// Samples texture (a TextureId's index) with one of the fixed samplers.
public float4 sampleTexture(uint texture, uint sampler, float2 uv) {
    return gTextures[NonUniformResourceIndex(texture)].Sample(gSamplers[NonUniformResourceIndex(sampler)], uv);
}
