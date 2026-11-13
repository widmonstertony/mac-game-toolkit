#pragma once
#include <stdint.h>

// An owned diagnostic ABI, not a reinterpretation of a vendor structure.
struct ChainResource {
    uint64_t native, sequence;
    uint32_t type, lifecycle, state, format;
    uint32_t width, height, left, top, extentWidth, extentHeight;
};
struct ChainFrame {
    uint64_t frame, constantsSequence, evaluateSequence;
    uint32_t viewport, hasConstants, tagsMask;
    float jitter[2], scale[2], nearPlane, farPlane, fov, aspect;
    uint32_t inverted, cameraMotionIncluded, motion3D, dilated, jittered, reset;
    float clipToPrevious[16], projection[16], cameraBasis[12];
    ChainResource tags[6]; // depth, velocity, hudless, UI, scaling input/output color
};
typedef int (__cdecl *ChainGet)(uint64_t, ChainFrame *);
typedef uint64_t (__cdecl *ChainEvaluateFrame)();
typedef void (__cdecl *ChainReport)(const char *);
