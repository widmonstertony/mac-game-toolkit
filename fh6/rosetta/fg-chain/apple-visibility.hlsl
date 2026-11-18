// Repair only a verified disocclusion in the generated frame. The two depth
// and motion pairs are immutable snapshots from the same Prepare epochs.
Texture2D<float> currentDepth : register(t0);
Texture2D<float2> currentMotion : register(t1);
Texture2D<float> previousDepth : register(t2);
Texture2D<float2> previousMotion : register(t3);
Texture2D<float4> currentScene : register(t4);
RWTexture2D<uint> confidence : register(u0);
RWTexture2D<float4> generated : register(u1);

cbuffer Frame : register(b0) {
    uint2 renderSize;
    uint2 displaySize;
    row_major float4x4 clipToPrevious;
    float2 scale;
    uint resolve;
};

int2 renderPixel(float2 p) {
    return clamp(int2(p * renderSize / displaySize), 0, int2(renderSize) - 1);
}

bool inFrame(float2 p) {
    return all(p >= .5) && all(p < float2(displaySize) - .5);
}

float2 motionAt(float2 p) {
    return currentMotion.Load(int3(renderPixel(p), 0)) * scale;
}

float depthAt(float2 p) {
    return currentDepth.Load(int3(renderPixel(p), 0));
}

float2 viewportEdge(float2 p) {
    return clamp(p, .5, float2(displaySize) - 1.5);
}

float2 projectedMotion(float2 p) {
    float2 edge = viewportEdge(p);
    float2 v = motionAt(edge);
    float2 inner = 2 * edge - p;
    if (any(p != edge) && inFrame(inner)) {
        float2 gradient = v - motionAt(inner);
        if (length(gradient) < length(v) * .35) v += gradient;
    }
    return v;
}

float2 sourceAt(float2 midpoint) {
    float2 current = midpoint;
    [loop] for (int i = 0; i < 7; ++i) {
        float2 v = projectedMotion(current);
        if (!all(abs(v) <= 1024)) return midpoint;
        current = midpoint - .5 * v;
    }
    return current;
}

bool cameraPrevious(float2 p, float depth, out float3 projected) {
    float2 clip = p / displaySize * float2(2, -2) + float2(-1, 1);
    float4 old = mul(float4(clip, depth, 1), clipToPrevious);
    projected = 0;
    if (!(old.z > 0 && old.w > 0)) return false;
    projected = float3((old.xy / old.w * float2(.5, -.5) + .5) * displaySize, old.z / old.w);
    return true;
}

// At the bottom of a fast chase view the correct current road coordinate
// leaves the image. Reject an unproven extrapolated color. The visible road
// at the output coordinate is a safe donor when its own camera reprojection
// is consistent and puts it behind a slow foreground in the older frame.
bool edgeRoad(float2 midpoint, out float2 prior, out float previousZ, out float2 velocity, out float fade) {
    prior = 0;
    previousZ = 0;
    fade = 0;
    velocity = 0;
    if (midpoint.y < displaySize.y - 512.5) return false;
    velocity = motionAt(midpoint);
    if (!(velocity.y < -24 && all(abs(velocity) < 1024))) return false;
    float2 current = midpoint - .5 * velocity;
    float beyond = current.y - (displaySize.y - .5);
    if (!(beyond > 0 && beyond < .12 * displaySize.y && current.x >= .5 && current.x < displaySize.x - .5))
        return false;
    float z = depthAt(midpoint);
    float3 projected;
    if (!(z > 0 && z <= 1) || !cameraPrevious(midpoint, z, projected) ||
        !inFrame(projected.xy) || length(projected.xy - (midpoint + velocity)) >= 1.75) return false;
    prior = projected.xy;
    previousZ = projected.z;
    fade = smoothstep(0, 22, beyond);
    return true;
}

float edgeConfidence(float2 midpoint) {
    float2 prior, velocity;
    float expected, fade;
    if (!edgeRoad(midpoint, prior, expected, velocity, fade)) return 0;
    int2 pixel = renderPixel(prior);
    float oldZ = previousDepth.Load(int3(pixel, 0));
    float2 oldMotion = previousMotion.Load(int3(pixel, 0)) * scale;
    float speed = length(velocity);
    if (!(oldZ > 0 && oldZ <= 1 && length(oldMotion) < min(24.0, speed * .25))) return 0;
    return fade * smoothstep(1.02, 1.10, oldZ / expected);
}

bool inspect(float2 midpoint, out float2 donor, out float weight) {
    donor = midpoint;
    weight = 0;
    // The road has substantial screen motion. This prefilter excludes sky,
    // the car itself and inspection of static scene geometry.
    float initialSpeed = length(motionAt(midpoint));
    if (!(initialSpeed >= 12 && initialSpeed <= 1024)) return false;
    float2 current = sourceAt(midpoint);
    float2 edge = viewportEdge(current);
    float edgeDistance = length(current - edge);
    float maxEdge = min(256.0, .11 * min(displaySize.x, displaySize.y));
    if (!(edgeDistance <= maxEdge)) return false;
    float2 velocity = projectedMotion(current);
    float speed = length(velocity);
    float2 previous = current + velocity;
    if (!(speed >= 16 && speed <= 1024) || !inFrame(previous) ||
        length(current + .5 * velocity - midpoint) >= .8) return false;
    float z = depthAt(edge);
    if (!(z > 0 && z <= 1)) return false;
    if (edgeDistance > 0) {
        // Extend the depth of the visible surface only over a short distance
        // and only when a second sample supports the same local plane.
        float2 inner = 2 * edge - current;
        if (!inFrame(inner)) return false;
        float inwardDepth = depthAt(inner);
        if (!(inwardDepth > 0 && abs(inwardDepth - z) < z * .2)) return false;
        z = 2 * z - inwardDepth;
    }
    float3 projected;
    if (!cameraPrevious(current, z, projected)) return false;
    float error = length(projected.xy - previous);
    float tolerance = 1.75 + edgeDistance * .05;
    if (!(error < tolerance)) return false;

    int2 priorPixel = renderPixel(previous);
    float previousZ = previousDepth.Load(int3(priorPixel, 0));
    if (!(previousZ > 0 && previousZ <= 1)) return false;
    float2 occluder = previousMotion.Load(int3(priorPixel, 0)) * scale;
    if (!(length(occluder) <= min(24.0, speed * .25) &&
          length(occluder - velocity) >= speed * .65)) return false;

    float valid = (1 - smoothstep(.6 + edgeDistance * .035, tolerance, error)) *
                  (1 - smoothstep(.84 * maxEdge, maxEdge, edgeDistance));
    weight = smoothstep(1.01, 1.08, previousZ / projected.z) * valid;
    donor = current;
    return valid > 0;
}

float3 sceneAt(float2 p) {
    float2 base = clamp(p, .5, float2(displaySize) - .5) - .5;
    int2 q = int2(base);
    int2 last = int2(displaySize) - 1;
    float2 f = frac(base);
    float3 a = currentScene.Load(int3(q, 0)).rgb;
    float3 b = currentScene.Load(int3(min(q + int2(1, 0), last), 0)).rgb;
    float3 c = currentScene.Load(int3(min(q + int2(0, 1), last), 0)).rgb;
    float3 d = currentScene.Load(int3(min(q + 1, last), 0)).rgb;
    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

float2 confidenceAt(int2 p) {
    uint value = confidence[clamp(p, 0, int2(renderSize) - 1)];
    return float2(value & 255, value >> 8) / 255.0;
}

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (!resolve) {
        if (any(id.xy >= renderSize)) return;
        float2 midpoint = (float2(id.xy) + .5) * displaySize / renderSize;
        float2 donor;
        float weight;
        inspect(midpoint, donor, weight);
        uint ordinary = uint(saturate(weight) * 255 + .5);
        uint border = uint(saturate(edgeConfidence(midpoint)) * 255 + .5);
        confidence[id.xy] = ordinary | (border << 8);
        return;
    }
    if (any(id.xy >= displaySize)) return;
    float2 midpoint = float2(id.xy) + .5;
    int2 tile = renderPixel(midpoint);
    float2 feather = 0;
    [unroll] for (int y = -1; y <= 1; ++y) {
        [unroll] for (int x = -1; x <= 1; ++x)
            feather += (2 - abs(x)) * (2 - abs(y)) * confidenceAt(tile + 2 * int2(x, y));
    }
    if (max(feather.x, feather.y) <= .01) return;
    float edgeWeight = 0;
    if (feather.y > .01) {
        float2 prior, v;
        float expected, fade;
        if (edgeRoad(midpoint, prior, expected, v, fade))
            edgeWeight = fade * saturate(feather.y * (1.4 / 16));
    }
    float2 donor;
    float weight;
    bool ordinary = feather.x > .01 && inspect(midpoint, donor, weight);
    if (!ordinary) {
        if (edgeWeight > 0) {
            float4 value = generated[id.xy];
            value.rgb = lerp(value.rgb, sceneAt(midpoint), edgeWeight);
            generated[id.xy] = value;
        }
        return;
    }
    float2 edge = viewportEdge(donor);
    float2 v = motionAt(edge);
    float speed = length(v);
    float3 local = sceneAt(midpoint);
    float3 corrected = sceneAt(edge);
    // Fade to the visible road near edges. Do not stretch a single scanline.
    float distance = length(donor - edge);
    float border = min(min(edge.x, edge.y), min(displaySize.x - edge.x, displaySize.y - edge.y));
    float surfaceRatio = depthAt(midpoint) / depthAt(edge);
    bool sameRoad = length(motionAt(midpoint) - v) < speed * .33 &&
        surfaceRatio > .82 && surfaceRatio < 1.20;
    if (sameRoad) {
        corrected = lerp(corrected, local, smoothstep(0, 80, distance + max(0, 80 - border)));
        // Preserve coherent near-vehicle shadows that move differently from the road.
        float luma = dot(corrected, float3(.25, .5, .25));
        float shadow = dot(local, float3(.25, .5, .25));
        corrected *= min(1.0, (shadow + 4.0 / 255) / (luma + 4.0 / 255));
    } else if (distance > 0 && edgeWeight == 0) {
        return;
    }
    float4 value = generated[id.xy];
    float visibilityWeight = (distance == 0 || sameRoad) ? saturate(feather.x * (1.4 / 16)) : 0;
    value.rgb = lerp(lerp(value.rgb, corrected, visibilityWeight), local, edgeWeight);
    generated[id.xy] = value;
}
