// Owned generated-frame-only compositor. Streamline native UI is premultiplied.
Texture2D<float4> nativeUI : register(t0);
RWTexture2D<float4> generated : register(u0);
cbuffer Size : register(b0) { uint width; uint height; };
[numthreads(8,8,1)] void main(uint3 p : SV_DispatchThreadID) {
    if (p.x >= width || p.y >= height) return;
    float4 ui = nativeUI.Load(int3(p.xy, 0));
    float4 scene = generated[p.xy];
    generated[p.xy] = float4(ui.rgb + (1.0 - ui.a) * scene.rgb, 1.0);
}
