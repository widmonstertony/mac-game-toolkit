from pathlib import Path
import struct,json,math
import argparse
p=argparse.ArgumentParser();p.add_argument('older',type=Path);p.add_argument('current',type=Path);p.add_argument('output',type=Path);args=p.parse_args()
out=args.output;out.mkdir(exist_ok=True)
for i in range(2):
 src=[args.older,args.current][i]
 b=(src/'native-frame.bin').read_bytes();camera=struct.unpack_from('<8f',b,36);basis=struct.unpack_from('<12f',b,220)
 pos,up,right,forward=[basis[n:n+3] for n in range(0,12,3)]
 view=[right[0],up[0],forward[0],0,right[1],up[1],forward[1],0,right[2],up[2],forward[2],0,*[-sum(a*b for a,b in zip(pos,axis)) for axis in (right,up,forward)],1]
 near,far=camera[5],camera[4] # Inverted depth, as in OptiScaler XeFG.
 h=1/math.tan(camera[6]/2); w=h/camera[7]; z=far/(far-near)
 proj=[w,0,0,0,0,h,0,0,0,0,z,1,0,0,-z*near,0]
 metadata=json.loads((src/'capture.json').read_text())
 c=struct.pack('<36fIf',*view,*proj,*camera[:2],*metadata['prepare']['scale'],0,metadata['prepare']['milliseconds'])
 (out/f'constants-{i}.bin').write_bytes(c)
 for name,source,width,height,pitch in [('color','scene',3472,2266,14080),('depth','prepare-current-depth',2314,1510,9728),('motion','prepare-current-motion',2314,1510,9472)]:
  raw=(src/(source+'.bin')).read_bytes(); assert len(raw)==pitch*height
  (out/f'{name}-{i}.bin').write_bytes(b''.join(raw[y*pitch:y*pitch+width*4] for y in range(height)))
print('Prepared ordered inputs with same-frame prepare depth and motion; verify frame IDs and dimensions before replay.')
