#pragma once
#include <stdint.h>
struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12Resource;
struct FrameGen;
FrameGen *createFrameGen(const wchar_t *library, ID3D12Device *device, uint32_t rw, uint32_t rh, uint32_t uw, uint32_t uh,
                        ID3D12Resource *presentColor);
bool recordFrameGen(FrameGen *fg, ID3D12GraphicsCommandList *list, ID3D12Resource *depth, ID3D12Resource *motion,
                    ID3D12Resource *present, ID3D12Resource *generated, uint64_t frame);
void destroyFrameGen(FrameGen *fg);
