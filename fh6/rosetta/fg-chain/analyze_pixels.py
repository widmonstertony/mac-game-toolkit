#!/usr/bin/env python3
"""Analyze only GPU-fenced captures; export the actual captured pixels without PIL/NumPy."""
import argparse
import ctypes as C
import json
import math
import mmap
from pathlib import Path
import struct
import zlib


class Resource(C.Structure):
    _fields_ = [('pointer', C.c_uint64), ('sequence', C.c_uint64)] + [(n, C.c_uint32) for n in
        ('type', 'life', 'state', 'format', 'width', 'height', 'left', 'top', 'extent_width', 'extent_height')]


class Frame(C.Structure):
    _fields_ = [(n, C.c_uint64) for n in ('frame', 'constants_sequence', 'evaluate_sequence')] + [
        (n, C.c_uint32) for n in ('viewport', 'has_constants', 'tag_mask')] + [
        ('jitter', C.c_float * 2), ('scale', C.c_float * 2)] + [
        (n, C.c_float) for n in ('near', 'far', 'fov', 'aspect')] + [
        (n, C.c_uint32) for n in ('inverted', 'camera_motion', 'motion_3d', 'dilated', 'jittered', 'reset')] + [
        ('clip_to_previous', C.c_float * 16), ('projection', C.c_float * 16), ('camera_basis', C.c_float * 12),
        ('tags', Resource * 6)]


class Image:
    def __init__(self, folder, info):
        self.info = info
        self.file = (folder / (info['name'] + '.bin')).open('rb')
        self.data = mmap.mmap(self.file.fileno(), 0, access=mmap.ACCESS_READ)
        if len(self.data) != info['bytes']:
            raise ValueError('Truncated capture: ' + info['name'])
        self.width, self.height, self.pitch = info['width'], info['height'], info['row_pitch']
        self.stride = info['row_bytes'] // self.width
        if self.stride * self.width != info['row_bytes']:
            raise ValueError('Unknown packed pixel layout')

    def rgba(self, x, y):
        if self.info['source_format'] != 28 or self.stride != 4:
            raise ValueError('Expected captured RGBA8 UNORM')
        return struct.unpack_from('4B', self.data, y * self.pitch + x * 4)

    def motion(self, x, y):
        if self.info['source_format'] != 34 or self.stride != 4:
            raise ValueError('Expected captured RG16 float')
        return struct.unpack_from('ee', self.data, y * self.pitch + x * 4)

    def depth(self, x, y):
        if self.info['source_format'] not in (19, 41) or self.stride not in (4, 8):
            raise ValueError('Unknown depth plane layout')
        return struct.unpack_from('f', self.data, y * self.pitch + x * self.stride)[0]

    def png(self, path):
        # Readback row layout comes from GetCopyableFootprints; never infer a
        # depth-plane stride from its DXGI format name (the failed probe did).
        if self.info['source_format'] != 28 or self.stride != 4:
            return
        def chunk(f, kind, data):
            f.write(struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data)))
        with path.open('wb') as f:
            f.write(b'\x89PNG\r\n\x1a\n')
            chunk(f, b'IHDR', struct.pack('>2I5B', self.width, self.height, 8, 6, 0, 0, 0))
            compressor = zlib.compressobj(3)
            for y in range(self.height):
                part = compressor.compress(b'\0' + self.data[y*self.pitch:y*self.pitch+self.width*4])
                if part:
                    chunk(f, b'IDAT', part)
            chunk(f, b'IDAT', compressor.flush())
            chunk(f, b'IEND', b'')


def percentile(values, p):
    values = sorted(values)
    return values[min(len(values)-1, int((len(values)-1)*p))] if values else None


def analyze(folder, png=False):
    meta = json.loads((folder / 'capture.json').read_text())
    if not meta['gpu_fence_completed'] or not meta['pixel_files_complete']:
        raise ValueError('No completed GPU evidence')
    blob = (folder / 'native-frame.bin').read_bytes()
    if len(blob) != C.sizeof(Frame) or meta['native_frame_size'] != C.sizeof(Frame):
        raise ValueError('Unknown native frame ABI')
    native = Frame.from_buffer_copy(blob)
    if native.frame != meta['frame'] or not meta['prepare']['known'] or meta['prepare']['frame'] != meta['frame']:
        raise ValueError('Frame identity is incomplete')
    images = {i['name']: Image(folder, i) for i in meta['images']}
    scene, ui, final, before, after = [images[n] for n in ('scene', 'ui', 'present', 'generated-before-ui', 'generated-after-ui')]
    if len({(i.width, i.height) for i in (scene, ui, final, before, after)}) != 1:
        raise ValueError('Color capture dimensions differ')
    counts = dict(samples=0, transparent_ui=0, transparent_ui_with_rgb=0,
                  scene_final_difference_outside_ui=0, expected_native_ui_mismatch=0,
                  generated_changed_by_ui=0, generated_differs_from_final=0)
    composition_error = 0
    for y in range(0, final.height, 8):
        for x in range(0, final.width, 8):
            s, u, b, a, p = scene.rgba(x,y), ui.rgba(x,y), before.rgba(x,y), after.rgba(x,y), final.rgba(x,y)
            counts['samples'] += 1
            if u[3] <= 1:
                counts['transparent_ui'] += 1
                counts['transparent_ui_with_rgb'] += max(u[:3]) > 2
                counts['scene_final_difference_outside_ui'] += max(abs(s[k]-p[k]) for k in range(3)) > 3
            error = max(abs(a[k]-min(255,max(0,u[k]+(1-u[3]/255)*b[k]))) for k in range(3))
            composition_error = max(error, composition_error)
            counts['expected_native_ui_mismatch'] += error > 2
            counts['generated_changed_by_ui'] += max(abs(a[k]-b[k]) for k in range(3)) > 3
            counts['generated_differs_from_final'] += max(abs(a[k]-p[k]) for k in range(3)) > 3
    motion, depth = images['motion'], images['depth']
    magnitude, z_values = [], []
    invalid_motion = invalid_depth = 0
    scale = meta['prepare']['scale']
    for y in range(0, motion.height, 8):
        for x in range(0, motion.width, 8):
            vx, vy = motion.motion(x,y)
            if not math.isfinite(vx) or not math.isfinite(vy):
                invalid_motion += 1
            else:
                magnitude.append(math.hypot(vx*scale[0],vy*scale[1]))
    for y in range(0, depth.height, 8):
        for x in range(0, depth.width, 8):
            z = depth.depth(x,y)
            if not math.isfinite(z):
                invalid_depth += 1
            else:
                z_values.append(z)
    probes=[]
    for fx,fy in ((.4,.65),(.5,.65),(.6,.65),(.4,.8),(.5,.8),(.6,.8)):
        x,y=min(motion.width-1,int(fx*motion.width)),min(motion.height-1,int(fy*motion.height))
        raw=motion.motion(x,y)
        probes.append(dict(render_xy=[x,y],raw=list(raw),display_pixel_motion=[raw[0]*scale[0]*final.width/motion.width,raw[1]*scale[1]*final.height/motion.height]))
    result={'frame':meta['frame'],'gpu_fence_completed':True,'counts_sampled_every_8_pixels':counts,
            'max_native_ui_composition_error_8bit':composition_error,
            'frame_milliseconds':meta['prepare']['milliseconds'],'native_jitter':list(native.jitter),
            'native_scale':list(native.scale),'fg_scale':scale,
            'native_flags':{n:getattr(native,n) for n in ('inverted','camera_motion','motion_3d','dilated','jittered','reset')},
            'motion':{'invalid_samples':invalid_motion,'render_pixel_magnitude_p50_p90_p99':[percentile(magnitude,p) for p in (.5,.9,.99)],'probes':probes},
            'depth':{'invalid_samples':invalid_depth,'min':min(z_values) if z_values else None,'max':max(z_values) if z_values else None,'copy_pixel_stride':depth.stride},
            'limits':'Pixel evidence distinguishes input, FG and UI stages. A nonzero difference is not by itself proof of ghosting, wrong MV sign or a displayed-frame pacing fault.'}
    if png:
        for name in ('scene','ui','present','generated-before-ui','generated-after-ui'):
            images[name].png(folder/(name+'.png'))
    (folder/'pixel-analysis.json').write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    return result


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('folder',type=Path)
    p.add_argument('--png',action='store_true',help='Export actual RGBA8 buffers to PNG for visual comparison')
    args=p.parse_args()
    print(json.dumps(analyze(args.folder,args.png),indent=2,allow_nan=False))


if __name__=='__main__':
    main()
