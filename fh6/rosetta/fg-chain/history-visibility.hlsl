// Experimental visibility control for saved-frame GPU replay only.
// Keeps official FG running. Rejects a previous-frame sample only when the
// current branch is consistent, static-world camera reprojection agrees, and
// actual previous depth proves that branch was occluded. No car/screen mask.
Texture2D<uint> fieldX : register(t0);
Texture2D<uint> fieldY : register(t1);
Texture2D<float> previousDepth : register(t2);
Texture2D<float2> previousMotion : register(t3);
Texture2D<float> currentDepth : register(t4);
Texture2D<float2> currentMotion : register(t5);
RWTexture2D<float2> visibility : register(u0);
cbuffer Input : register(b0) {
    uint2 renderSize;
    uint2 displaySize;
    row_major float4x4 clipToPrevious;
    float2 motionScale;
    uint2 padding;
};
[numthreads(8,8,1)] void main(uint3 p : SV_DispatchThreadID) {
    if (any(p.xy < 2) || any(p.xy >= renderSize - 2)) return;
    float2 valid = visibility[p.xy];
    if (any(valid != 1.0)) return;
    uint packed = fieldX.Load(int3(p.xy, 0));
    if (((packed >> 21) & 1023) == 0) return;
    float2 halfMotion = float2(f16tof32(packed & 65535),
                              f16tof32(fieldY.Load(int3(p.xy, 0)) & 65535));
    float2 expected = halfMotion * 2.0;
    float speed = length(expected * displaySize);
    if (!all(abs(halfMotion) <= 3.4e38) || speed < 8.0) return;
    float2 current = float2(p.xy) - halfMotion * renderSize;
    float2 previous = float2(p.xy) + halfMotion * renderSize;
    if (any(current <= 1) || any(previous <= 1) ||
        any(current >= renderSize - 2) || any(previous >= renderSize - 2)) return;
    float2 mv = currentMotion.Load(int3(int2(round(current)), 0)) * motionScale;
    float2 prior = previousMotion.Load(int3(int2(round(previous)), 0)) * motionScale;
    if (length((mv - expected) * displaySize) > max(2.0, speed * 0.1) ||
        length((prior - expected) * displaySize) < max(8.0, speed * 0.25)) return;
    float depth = currentDepth.Load(int3(int2(current), 0));
    float4 clip = float4(current.x / renderSize.x * 2 - 1,
                         1 - current.y / renderSize.y * 2, depth, 1);
    float4 oldClip = mul(clip, clipToPrevious);
    if (!all(abs(oldClip) <= 3.4e38) || oldClip.w <= 0) return;
    float3 projected = oldClip.xyz / oldClip.w;
    float2 oldPixel = float2(projected.x * 0.5 + 0.5,
                            0.5 - projected.y * 0.5) * renderSize;
    if (length(oldPixel - current - mv * renderSize) > 1.0) return;
    float visibleDepth = previousDepth.Load(int3(int2(previous), 0));
    // Reversed Z: larger depth is nearer. Conservative 1% margin is an
    // experimental control, not a validated universal production threshold.
    if (abs(visibleDepth) <= 3.4e38 && projected.z > 0 && visibleDepth > projected.z * 1.01)
        visibility[p.xy] = float2(0, valid.y);
}
