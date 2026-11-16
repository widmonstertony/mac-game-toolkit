#!/usr/bin/env python3
"""Compare recorded backward motion against camera-only depth reprojection.

This is a diagnostic, not a verifier for arbitrary moving objects. A car moving
with the camera should have a large residual from camera-only motion. Input
coordinates are in output-color pixels; nearest depth/MV texels are reported.
No game files are changed and no captures are uploaded.
"""
import argparse,json,math,struct
from pathlib import Path

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('capture',type=Path)
    parser.add_argument('--point',action='append',required=True,help='x,y output-pixel coordinate')
    args=parser.parse_args();root=args.capture
    meta=json.loads((root/'capture.json').read_text())
    native=(root/'native-frame.bin').read_bytes()
    frame=meta['frame']
    if len(native)!=608 or struct.unpack_from('<Q',native)[0]!=frame:
        raise SystemExit('Native metadata frame mismatch')
    if not meta.get('gpu_fence_completed') or not meta.get('pixel_files_complete'):
        raise SystemExit('Incomplete capture')
    images={i['name']:i for i in meta['images']}
    color,depth,motion=(images[k] for k in ('scene','prepare-current-depth','prepare-current-motion'))
    for i in (color,depth,motion):
        if i['source_frame']!=frame:raise SystemExit('Input resource frame mismatch')
    w,h=color['width'],color['height'];rw,rh=depth['width'],depth['height']
    if (motion['width'],motion['height'])!=(rw,rh):raise SystemExit('Depth/MV grid mismatch')
    d=(root/'prepare-current-depth.bin').read_bytes();m=(root/'prepare-current-motion.bin').read_bytes()
    if len(d)!=depth['row_pitch']*rh or len(m)!=motion['row_pitch']*rh:
        raise SystemExit('Input byte size mismatch')
    raw=struct.unpack_from('<16f',native,92)
    # Streamline row-vector storage becomes a column-vector matrix.
    clip=[[raw[j*4+i] for j in range(4)] for i in range(4)]
    scale=struct.unpack_from('<2f',native,44)
    output=[]
    for point in args.point:
        x,y=map(int,point.split(','))
        if not (0<=x<w and 0<=y<h):raise SystemExit('Point outside color extent')
        rx,ry=int(x*rw/w),int(y*rh/h)
        z=struct.unpack_from('<f',d,ry*depth['row_pitch']+rx*4)[0]
        mx,my=struct.unpack_from('<ee',m,ry*motion['row_pitch']+rx*4)
        v=[(rx+.5)/rw*2-1,1-(ry+.5)/rh*2,z,1]
        q=[sum(clip[i][j]*v[j] for j in range(4)) for i in range(4)]
        if not all(map(math.isfinite,[z,mx,my,*q])) or abs(q[3])<1e-8:
            raise SystemExit('Nonfinite input or invalid homogeneous coordinate')
        raw_flow=[mx*scale[0]*w,my*scale[1]*h]
        camera=[(q[0]/q[3]-v[0])*w*.5,-(q[1]/q[3]-v[1])*h*.5]
        output.append(dict(point=[x,y],inputTexel=[rx,ry],depth=z,motionPixels=raw_flow,
                           cameraPixels=camera,residualPixels=[a-b for a,b in zip(raw_flow,camera)]))
    print(json.dumps(dict(frame=frame,color=[w,h],input=[rw,rh],points=output),indent=2))
if __name__=='__main__':main()
