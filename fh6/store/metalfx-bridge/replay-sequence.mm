// Continuous native MetalFX replay. No repeated/frozen history initialization.
#include "bridge-native.mm"
#include "test-buffer.h"
#include <cstdio>
#include "replay-common.h"
int main(int argc,char **argv){@autoreleasepool{
 if(argc!=3){fprintf(stderr,"usage: replay-sequence sequence-folder new-output-folder\n");return 1;}
 NSString *root=[NSString stringWithUTF8String:argv[1]],*out=[NSString stringWithUTF8String:argv[2]];
 NSData *receipt=read(root,@"sequence.json");if(!receipt)return 2;
 NSDictionary *summary=[NSJSONSerialization JSONObjectWithData:receipt options:0 error:nil];
 if(![summary isKindOfClass:NSDictionary.class]||![summary[@"complete"] boolValue])return 2;
 uint64_t first=[summary[@"first"] unsignedLongLongValue],last=[summary[@"last"] unsignedLongLongValue];
 unsigned count=[summary[@"frames"] unsignedIntValue];if(count!=8||last<first||last-first+1!=count)return 2;
 NSMutableArray *metas=[NSMutableArray array],*folders=[NSMutableArray array];
 for(unsigned n=0;n<count;++n){NSString *folder=[root stringByAppendingPathComponent:[NSString stringWithFormat:@"frame-%llu",(unsigned long long)(first+n)]];
  NSDictionary *meta=metadata(folder);if(!meta||[meta[@"frame"] unsignedLongLongValue]!=first+n)return 3;
  [folders addObject:folder];[metas addObject:meta];}
 NSArray *firstNames=replayInputNames(metas[0]);
 NSDictionary *ci=imageInfo(metas[0],firstNames[0]),*di=imageInfo(metas[0],firstNames[1]);
 unsigned w=[ci[@"width"] unsignedIntValue],h=[ci[@"height"] unsignedIntValue],rw=[di[@"width"] unsignedIntValue],rh=[di[@"height"] unsignedIntValue];
 if(!w||!h||!rw||!rh||w>16384||h>16384||rw>w||rh>h)return 3;
 if([[NSFileManager defaultManager] fileExistsAtPath:out]||![[NSFileManager defaultManager] createDirectoryAtPath:out withIntermediateDirectories:NO attributes:nil error:nil])return 4;
 FxCreate c{1,rw,rh,w,h,0,0};if(create(&c))return 5;
 OwnedBuffer color(w,h,FX_RGBA8),depth(rw,rh,FX_R32F),motion(rw,rh,FX_RG16F),output(w,h,FX_RGBA8);
 FxDispatchCamera p{};p.frame.context=c.context;p.frame.color=color.b;p.frame.depth=depth.b;p.frame.motion=motion.b;p.frame.output=output.b;
 unsigned generated=0;NSMutableArray *results=[NSMutableArray array];
 for(unsigned n=0;n<count;++n){NSDictionary *meta=metas[n];NSString *folder=folders[n];
  NSArray *names=replayInputNames(meta);
  OwnedBuffer *buffers[]={&color,&depth,&motion};
  for(unsigned k=0;k<3;++k){NSDictionary *info=imageInfo(meta,names[k]);
   if([info[@"width"] unsignedIntValue]!=buffers[k]->b.width||[info[@"height"] unsignedIntValue]!=buffers[k]->b.height||
      [info[@"row_bytes"] unsignedLongLongValue]!=buffers[k]->b.width*4||
      !copy(*buffers[k],read(folder,[names[k] stringByAppendingString:@".bin"]),[info[@"row_pitch"] unsignedLongLongValue],buffers[k]->b.width,buffers[k]->b.height))return 6;}
  NSData *native=read(folder,@"native-frame.bin");if(!loadCamera(native,p))return 6;
  uint32_t flags[6];memcpy(flags,(const char*)native.bytes+68,sizeof(flags));
  if(flags[0]>1||flags[1]!=1||flags[2]!=0||flags[4]!=0||flags[5]!=0)return 6;
  p.frame.frameId=first+n;p.frame.reset=n==0;p.frame.depthReversed=flags[0];
  p.frame.deltaSeconds=[meta[@"prepare"][@"milliseconds"] floatValue]*.001f;
  int result=dispatchCamera(&p);printf("frame=%llu result=%d generated=%u gpu_ms=%.3f\n",(unsigned long long)p.frame.frameId,result,p.frame.generated,p.frame.gpuMilliseconds);
  if(result||p.frame.generated!=(n>=2))return 7;
  generated+=p.frame.generated;
  NSString *file=[out stringByAppendingPathComponent:[NSString stringWithFormat:@"frame-%llu.rgba",(unsigned long long)p.frame.frameId]];
  FILE *f=fopen(file.fileSystemRepresentation,"wb");if(!f)return 8;bool ok=true;for(unsigned y=0;y<h;++y)ok=(fwrite(output.row<uint8_t>(y),1,w*4,f)==w*4)&&ok;ok=fclose(f)==0&&ok;if(!ok)return 8;
  [results addObject:@{@"frame":@(p.frame.frameId),@"generated":@(p.frame.generated),@"gpu_ms":@(p.frame.gpuMilliseconds)}];
 }
 FxDestroy d{c.context};if(destroy(&d))return 9;
 NSDictionary *r=@{@"continuous_input_frames":@(count),@"warmup_current_copies":@2,@"generated_frames":@(generated),@"quality_verified":@NO,@"results":results};
 NSData *json=[NSJSONSerialization dataWithJSONObject:r options:NSJSONWritingPrettyPrinted error:nil];
 if(![json writeToFile:[out stringByAppendingPathComponent:@"replay.json"] atomically:YES])return 10;
 puts("PASS continuous real GPU replay; image quality still requires comparison");return 0;
}}
