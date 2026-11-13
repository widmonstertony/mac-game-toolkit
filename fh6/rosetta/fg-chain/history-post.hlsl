// Generated-frame-only visibility repair. Native UI is composed afterwards.
Texture2D<float> currentDepth : register(t0);
Texture2D<float2> currentMotion : register(t1);
Texture2D<float> previousDepth : register(t2);
Texture2D<float2> previousMotion : register(t3);
Texture2D<float4> currentScene : register(t4);
RWTexture2D<float4> generated : register(u0);
cbuffer Frame : register(b0) {
 uint2 renderSize;
 uint2 displaySize;
 row_major float4x4 clipToPrevious;
 float2 motionScale;
 uint2 padding;
};
float2 velocity(float2 uv, bool previous) {
 int2 p=clamp(int2(uv*renderSize),int2(0,0),int2(renderSize)-1);
 return (previous ? previousMotion.Load(int3(p,0)) : currentMotion.Load(int3(p,0))) * motionScale;
}
float3 scene(float2 uv) {
 float2 p=uv*displaySize-.5;int2 base=int2(floor(p));float2 f=frac(p);
 int2 end=int2(displaySize)-1;
 float3 a=currentScene.Load(int3(clamp(base,0,end),0)).rgb;
 float3 b=currentScene.Load(int3(clamp(base+int2(1,0),0,end),0)).rgb;
 float3 c=currentScene.Load(int3(clamp(base+int2(0,1),0,end),0)).rgb;
 float3 d=currentScene.Load(int3(clamp(base+int2(1,1),0,end),0)).rgb;
 return lerp(lerp(a,b,f.x),lerp(c,d,f.x),f.y);
}
[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID) {
 if(any(id.xy>=displaySize))return;
 float2 uv=(float2(id.xy)+.5)/displaySize;
 float2 current=uv;
 // Invert the current frame's motion to find its halfway-visible world point.
 // Reject nonconvergence instead of guessing a vector across an object edge.
 [unroll] for(uint i=0;i<6;i++) {
  float2 step=velocity(current,false);
  if(!all(abs(step)<=3.4e38))return;
  current=uv-step*.5;
  if(any(current<0)||any(current>=1))return;
 }
 if(any(current*renderSize<=1)||any(current*renderSize>=renderSize-2))return;
 float2 mv=velocity(current,false);float speed=length(mv*displaySize);
 if(!all(abs(mv)<=3.4e38)||speed<8)return;
 if(length((current+mv*.5-uv)*displaySize)>1.0)return;
 float2 previous=current+mv;
 if(any(previous*renderSize<=1)||any(previous*renderSize>=renderSize-2))return;
 float2 oldMotion=velocity(previous,true);
 if(length((oldMotion-mv)*displaySize)<max(8.0,speed*.25))return;
 float depth=currentDepth.Load(int3(int2(current*renderSize),0));
 float4 clip=float4(current.x*2-1,1-current.y*2,depth,1);
 float4 oldClip=mul(clip,clipToPrevious);
 if(!all(abs(oldClip)<=3.4e38)||oldClip.w<=0)return;
 float3 projected=oldClip.xyz/oldClip.w;
 float2 cameraPrevious=float2(projected.x*.5+.5,.5-projected.y*.5);
 if(length((cameraPrevious-current-mv)*renderSize)>1.0)return;
 float visible=previousDepth.Load(int3(int2(previous*renderSize),0));
 if(!(projected.z>0&&visible>projected.z*1.01))return;
 float4 value=generated[id.xy];value.rgb=scene(current);generated[id.xy]=value;
}
