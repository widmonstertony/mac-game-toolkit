#pragma once
#include <cfloat>
// Strict private-capture readers shared by pair and continuous replay.
static NSData *read(NSString *folder,NSString *name) {
    return [NSData dataWithContentsOfFile:[folder stringByAppendingPathComponent:name]];
}
static bool hasConsumedInputs(NSDictionary *meta){
    for(NSDictionary *i in meta[@"images"])if([i[@"name"] isEqual:@"consumed-prepare-depth"])return true;
    return false;
}
static NSArray *replayInputNames(NSDictionary *meta){
    return hasConsumedInputs(meta)?@[@"scene-before-fg",@"consumed-prepare-depth",@"consumed-prepare-motion"]:
                                  @[@"scene",@"prepare-current-depth",@"prepare-current-motion"];
}
static NSDictionary *metadata(NSString *folder,bool taggedInputControl=false) {
    NSData *data=read(folder,@"capture.json");if(!data)return nil;
    id object=[NSJSONSerialization JSONObjectWithData:data options:0 error:nil];
    if(![object isKindOfClass:NSDictionary.class])return nil;
    NSDictionary *m=object;uint64_t frame=[m[@"frame"] unsignedLongLongValue];
    if(!frame||![m[@"gpu_fence_completed"] boolValue]||![m[@"pixel_files_complete"] boolValue]||
       ![m[@"prepare"][@"known"] boolValue]||[m[@"prepare"][@"frame"] unsignedLongLongValue]!=frame)return nil;
    NSData *native=read(folder,@"native-frame.bin");if(native.length!=608)return nil;
    uint64_t nativeFrame=0;memcpy(&nativeFrame,native.bytes,8);if(nativeFrame!=frame)return nil;
    if(!taggedInputControl&&hasConsumedInputs(m)&&![m[@"native_capture_path"] boolValue])return nil;
    NSArray *required=taggedInputControl?@[@"scene",@"depth",@"motion"]:replayInputNames(m);
    for(NSString *name in required) {
        bool found=false;for(NSDictionary *image in m[@"images"])if([image[@"name"] isEqual:name]) {
            if(!taggedInputControl&&[image[@"source_frame"] unsignedLongLongValue]!=frame)return nil;
            if(!taggedInputControl&&hasConsumedInputs(m)&&![image[@"copy_stage"] isEqual:@"before-native-evaluate"])return nil;
            found=true;break;
        }
        if(!found)return nil;
    }
    return m;
}
static bool copy(OwnedBuffer &buffer,NSData *data,size_t sourcePitch,unsigned sourceW,unsigned sourceH) {
    if(data.length!=sourcePitch*sourceH||sourcePitch<sourceW*4)return false;
    for(unsigned y=0;y<buffer.b.height;++y){
        auto src=(const uint8_t*)data.bytes+(uint64_t(y)*2+1)*sourceH/(buffer.b.height*2)*sourcePitch;
        if(buffer.b.width==sourceW)memcpy(buffer.row<uint8_t>(y),src,buffer.b.width*4);
        else for(unsigned x=0;x<buffer.b.width;++x)memcpy(buffer.row<uint8_t>(y)+x*4,src+(uint64_t(x)*2+1)*sourceW/(buffer.b.width*2)*4,4);
    }
    return true;
}
static bool loadCamera(NSData *native,FxDispatchCamera &p) {
    if(native.length!=608)return false;
    float camera[8],basis[12];const auto b=(const uint8_t*)native.bytes;
    memcpy(camera,b+36,sizeof(camera));memcpy(basis,b+220,sizeof(basis));
    // Streamline's INVALID_FLOAT is finite FLT_MAX. Finiteness alone would
    // mistakenly accept unset jitter/camera position as a usable capture.
    for(float v:camera)if(!std::isfinite(v)||fabsf(v)==FLT_MAX)return false;
    for(float v:basis)if(!std::isfinite(v)||fabsf(v)==FLT_MAX)return false;
    auto &f=p.frame;f.jitterX=camera[0];f.jitterY=camera[1];
    f.motionScaleX=camera[2]*f.color.width;f.motionScaleY=camera[3]*f.color.height;
    f.nearPlane=camera[4];f.farPlane=camera[5];f.fovDegrees=camera[6]*180/M_PI;f.aspectRatio=camera[7];
    simd_float3 position={basis[0],basis[1],basis[2]},up={basis[3],basis[4],basis[5]},
        right={basis[6],basis[7],basis[8]},forward={basis[9],basis[10],basis[11]};
    simd_float4x4 view;
    view.columns[0]={right.x,up.x,forward.x,0};view.columns[1]={right.y,up.y,forward.y,0};
    view.columns[2]={right.z,up.z,forward.z,0};
    view.columns[3]={-simd_dot(right,position),-simd_dot(up,position),-simd_dot(forward,position),1};
    memcpy(p.camera.worldToView,&view,sizeof(view));
    // Streamline row-vector rows become column-vector SIMD columns: transpose
    // mathematically, with the same contiguous storage values.
    memcpy(p.camera.viewToClip,b+156,sizeof(p.camera.viewToClip));p.camera.flags=1;
    return true;
}
