// Generated-frame-only visibility repair. Native UI is composed afterwards.
Texture2D<float> currentDepth : register(t0);
Texture2D<float2> currentMotion : register(t1);
Texture2D<float> previousDepth : register(t2);
Texture2D<float2> previousMotion : register(t3);
Texture2D<float4> currentScene : register(t4);
Texture2D<float4> previousScene : register(t5);
RWTexture2D<float4> generated : register(u0);
// One atomic entry keeps the depth priority and source pixel together. Separate
// atomic X/Y components could select vectors from different competing sources.
RWTexture2D<uint> halfSources : register(u1);
cbuffer Frame : register(b0) {
 uint2 renderSize;
 uint2 displaySize;
 row_major float4x4 clipToPrevious;
 float2 motionScale;
 uint operation;
 uint previousColorKnown;
};
float2 velocity(float2 uv, bool previous) {
 int2 p=clamp(int2(uv*renderSize),int2(0,0),int2(renderSize)-1);
 return (previous ? previousMotion.Load(int3(p,0)) : currentMotion.Load(int3(p,0))) * motionScale;
}
float2 dilatedVelocity(int2 p, out float depth) {
 int2 chosen=p;depth=0;
 [unroll] for(int y=-1;y<=1;y++) [unroll] for(int x=-1;x<=1;x++) {
  int2 q=clamp(p+int2(x,y),int2(0,0),int2(renderSize)-1);
  float z=currentDepth.Load(int3(q,0));
  if(z>depth&&z<=1){depth=z;chosen=q;}
 }
 return currentMotion.Load(int3(chosen,0))*motionScale;
}
float3 color(float2 uv, bool previous) {
 float2 p=uv*displaySize-.5;int2 base=int2(floor(p));float2 f=frac(p);
 int2 end=int2(displaySize)-1;
 float3 a=previous?previousScene.Load(int3(clamp(base,0,end),0)).rgb:currentScene.Load(int3(clamp(base,0,end),0)).rgb;
 float3 b=previous?previousScene.Load(int3(clamp(base+int2(1,0),0,end),0)).rgb:currentScene.Load(int3(clamp(base+int2(1,0),0,end),0)).rgb;
 float3 c=previous?previousScene.Load(int3(clamp(base+int2(0,1),0,end),0)).rgb:currentScene.Load(int3(clamp(base+int2(0,1),0,end),0)).rgb;
 float3 d=previous?previousScene.Load(int3(clamp(base+int2(1,1),0,end),0)).rgb:currentScene.Load(int3(clamp(base+int2(1,1),0,end),0)).rgb;
 return lerp(lerp(a,b,f.x),lerp(c,d,f.x),f.y);
}
// A primary source can leave the viewport. Never turn its last visible dark
// shadow into a second car outline. This conservative border fill is separate
// from depth-proven visibility: it is a photometric-confidence policy, not a
// claim that unobserved outside-frame texture can be reconstructed exactly.
bool borderColor(float2 uv,out float3 value) {
 value=0;if(!previousColorKnown)return false;
 float2 edge=uv,mv=velocity(uv,false),current=uv;
 [unroll] for(uint i=0;i<3;i++) {
  if(!all(abs(mv)<2))return false;
  current=uv-mv*.5;
  edge=clamp(current,1.5/renderSize,1-1.5/renderSize);
  mv=velocity(edge,false);
 }
 current=uv-mv*.5;
 if(all(current>=0)&&all(current<1))return false;
 if(length(mv*displaySize)<8)return false;
 float2 prior=uv+mv*.5;
 if(any(prior<=0)||any(prior>=1))return false;
 float depth=currentDepth.Load(int3(int2(edge*renderSize),0));
 float4 clip=float4(edge.x*2-1,1-edge.y*2,depth,1);
 float4 old=mul(clip,clipToPrevious);
 if(!all(abs(old)<3.4e38)||old.w<=0)return false;
 float2 projected=float2(old.x/old.w*.5+.5,.5-old.y/old.w*.5);
 if(length((projected-edge-mv)*renderSize)>1)return false;
 float3 now=color(edge,false),before=color(prior,true);
 float a=dot(now,float3(.2126,.7152,.0722)),b=dot(before,float3(.2126,.7152,.0722));
 // Only a strong stale-dark-source disagreement. Preserve ordinary lighting,
 // moving foregrounds, unknown history and the generated frame everywhere else.
 if(a<=.03||b>=a*.5)return false;
 float3 original=generated[uint2(uv*displaySize)].rgb;
 value=lerp(original,now,saturate((.5-b/a)*4));return true;
}
[numthreads(8,8,1)] void main(uint3 id:SV_DispatchThreadID) {
 if(operation==0) {
  if(all(id.xy<renderSize))halfSources[id.xy]=0;
  return;
 }
 if(operation==1) {
  if(any(id.xy>=renderSize))return;
  float depth;float2 mv=dilatedVelocity(int2(id.xy),depth);
  if(!(depth>0)||!all(abs(mv)<2))return;
  float2 halfUv=(float2(id.xy)+.5)/renderSize+mv*.5;
  int2 base=int2(floor(halfUv*renderSize-.5));
  // Current reversed-Z depth orders competing primary sources. The bounded
  // render-size guard in C++ reserves 22 bits for the actual source index.
  uint priority=1+uint(sqrt(saturate(depth))*1022);
  uint entry=(priority<<22)|(id.y*renderSize.x+id.x);
  [unroll] for(int y=0;y<2;y++) [unroll] for(int x=0;x<2;x++) {
   int2 q=base+int2(x,y);
   if(all(q>=0)&&all(q<int2(renderSize)))InterlockedMax(halfSources[q],entry);
  }
  return;
 }
 if(any(id.xy>=displaySize))return;
 float2 uv=(float2(id.xy)+.5)/displaySize;
 uint entry=halfSources[min(uint2(uv*renderSize),renderSize-1)];
 if((entry>>22)==0){float3 fill;if(borderColor(uv,fill)){float4 value=generated[id.xy];value.rgb=fill;generated[id.xy]=value;}return;}
 uint index=entry&0x3fffff;
 int2 source=int2(index%renderSize.x,index/renderSize.x);
 if(any(source>=int2(renderSize)))return;
 float sourceDepth;float2 field=dilatedVelocity(source,sourceDepth);
 if(!all(abs(field)<2))return;
 float2 current=uv-field*.5;
 if(any(current<0)||any(current>=1))return;
 if(length((current-(float2(source)+.5)/renderSize)*renderSize)>2)return;
 if(any(current*renderSize<=1)||any(current*renderSize>=renderSize-2))return;
 float2 mv=velocity(current,false);float speed=length(mv*displaySize);
 if(!all(abs(mv)<=3.4e38)||speed<8)return;
 if(length((mv-field)*displaySize)>max(2.0,speed*.1))return;
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
 float4 value=generated[id.xy];value.rgb=color(current,false);generated[id.xy]=value;
}
