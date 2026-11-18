#pragma once

#include "apple-frame-inputs.h"
#include "../../rosetta/fg-chain/apple-visibility-bytecode.h"

// Same direct queue as MetalFX and the existing native UI pass. The field
// lives across command lists, so order the prior resolve before its reuse.
struct AppleVisibility {
    ID3D12RootSignature *root{};
    ID3D12PipelineState *pipeline{};
    ID3D12Resource *field{};
    bool initialized{};
    uint64_t recorded{};
    const char *reason{"no consecutive inputs"};

    bool initialize(ID3D12Device *device, UINT width, UINT height) {
        if (initialized) return field != nullptr;
        initialized = true;
        reason = "visibility pipeline initialization";

        D3D12_DESCRIPTOR_RANGE ranges[] = {
            {D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 0, 0, 0},
            {D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, 5}
        };
        D3D12_ROOT_PARAMETER params[2]{};
        params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        params[0].DescriptorTable = {2, ranges};
        params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
        params[1].Constants = {0, 0, 23};
        D3D12_ROOT_SIGNATURE_DESC signature{};
        signature.NumParameters = 2;
        signature.pParameters = params;
        auto module = GetModuleHandleW(L"d3d12.dll");
        auto serialize = module ? (decltype(&D3D12SerializeRootSignature))
            GetProcAddress(module, "D3D12SerializeRootSignature") : nullptr;
        ID3DBlob *blob{};
        if (!serialize || FAILED(serialize(&signature, D3D_ROOT_SIGNATURE_VERSION_1, &blob, nullptr)))
            return false;
        auto status = device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(),
                                                  IID_ID3D12RootSignature, (void **)&root);
        blob->Release();
        if (FAILED(status)) return false;
        D3D12_COMPUTE_PIPELINE_STATE_DESC compute{};
        compute.pRootSignature = root;
        compute.CS = {appleVisibilityBytecode, sizeof(appleVisibilityBytecode)};
        if (FAILED(device->CreateComputePipelineState(&compute, IID_ID3D12PipelineState, (void **)&pipeline)))
            return false;

        D3D12_HEAP_PROPERTIES heap{};
        heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC desc{};
        desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        desc.Width = width;
        desc.Height = height;
        desc.Format = DXGI_FORMAT_R32_UINT;
        desc.DepthOrArraySize = desc.MipLevels = desc.SampleDesc.Count = 1;
        desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
        return SUCCEEDED(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS, nullptr, IID_ID3D12Resource, (void **)&field));
    }

    bool record(AppleFrameInputs &inputs, AppleFrameInputs::Slot &current,
                ID3D12GraphicsCommandList *list, ID3D12Resource *scene, ID3D12Resource *output) {
        auto previous = inputs.latest;
        const auto &frame = current.frame;
        if (!previous || previous->frame.frame + 1 != frame.frame ||
            frame.reset || current.previousRead || frame.inverted != 1 ||
            frame.scale[0] != previous->frame.scale[0] || frame.scale[1] != previous->frame.scale[1]) {
            reason = "no consecutive visibility inputs";
            return false;
        }
        for (float v : frame.clipToPrevious) {
            if (!std::isfinite(v) || fabsf(v) == FLT_MAX) {
                reason = "invalid visibility projection";
                return false;
            }
        }
        auto render = current.depth->GetDesc();
        auto display = scene->GetDesc();
        if (!AppleFrameInputs::sameShape(previous->depth, render) ||
            !AppleFrameInputs::sameShape(previous->motion, current.motion->GetDesc()) ||
            !initialize(inputs.device, UINT(render.Width), render.Height) ||
            field->GetDesc().Width != render.Width || field->GetDesc().Height != render.Height)
            return false;
        auto &heap = current.visibilityHeap;
        if (!heap) {
            D3D12_DESCRIPTOR_HEAP_DESC desc{};
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            desc.NumDescriptors = 7;
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            if (FAILED(inputs.device->CreateDescriptorHeap(&desc, IID_ID3D12DescriptorHeap, (void **)&heap))) {
                reason = "visibility descriptor allocation";
                return false;
            }
        }
        auto handle = heap->GetCPUDescriptorHandleForHeapStart();
        auto stride = inputs.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        ID3D12Resource *resources[] = {current.depth, current.motion, previous->depth, previous->motion, scene};
        for (auto resource : resources) {
            D3D12_SHADER_RESOURCE_VIEW_DESC view{};
            view.Format = resource->GetDesc().Format;
            if (view.Format == DXGI_FORMAT_R32G8X24_TYPELESS)
                view.Format = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
            view.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            view.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            view.Texture2D.MipLevels = 1;
            inputs.device->CreateShaderResourceView(resource, &view, handle);
            handle.ptr += stride;
        }
        D3D12_UNORDERED_ACCESS_VIEW_DESC view{};
        view.Format = DXGI_FORMAT_R32_UINT;
        view.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
        inputs.device->CreateUnorderedAccessView(field, nullptr, &view, handle);
        handle.ptr += stride;
        inputs.device->CreateUnorderedAccessView(output, nullptr, nullptr, handle);

        struct Constants {
            UINT rw, rh, w, h;
            float matrix[16], scale[2];
            UINT resolve;
        } args{UINT(render.Width), render.Height, UINT(display.Width), display.Height};
        static_assert(sizeof(Constants) == 23 * sizeof(UINT), "visibility shader constants");
        memcpy(args.matrix, frame.clipToPrevious, sizeof(args.matrix));
        args.scale[0] = frame.scale[0] * args.w;
        args.scale[1] = frame.scale[1] * args.h;

        current.previousRead = previous;
        ++previous->readers;
        D3D12_RESOURCE_BARRIER barriers[2]{};
        for (auto &barrier : barriers) barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
        barriers[0].UAV.pResource = field;
        barriers[1].UAV.pResource = output;
        list->ResourceBarrier(1, barriers);
        list->SetDescriptorHeaps(1, &heap);
        list->SetComputeRootSignature(root);
        list->SetPipelineState(pipeline);
        list->SetComputeRootDescriptorTable(0, heap->GetGPUDescriptorHandleForHeapStart());
        list->SetComputeRoot32BitConstants(1, 23, &args, 0);
        list->Dispatch((args.rw + 7) / 8, (args.rh + 7) / 8, 1);
        list->ResourceBarrier(2, barriers);
        args.resolve = 1;
        list->SetComputeRoot32BitConstant(1, args.resolve, 22);
        list->Dispatch((args.w + 7) / 8, (args.h + 7) / 8, 1);
        list->ResourceBarrier(1, barriers + 1);
        ++recorded;
        reason = "visibility repair recorded";
        return true;
    }

    void release() {
        if (field) field->Release();
        if (pipeline) pipeline->Release();
        if (root) root->Release();
        *this = {};
    }
};
