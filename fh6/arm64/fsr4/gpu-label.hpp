#pragma once

#include <vkd3d_d3d12.h>

class Fh6GpuLabel {
    ID3D12GraphicsCommandList *commandList;
public:
    template<size_t N>
    Fh6GpuLabel(void *list, const char (&label)[N]) : commandList(nullptr)
    {
        static const bool enabled = [] {
            WCHAR value[2];
            return GetEnvironmentVariableW(L"FH6_GPU_LABELS", value, 2) == 1 && value[0] == L'1';
        }();
        if (enabled && list) {
            commandList = static_cast<ID3D12GraphicsCommandList *>(list);
            // PIX ANSI event metadata. vkd3d forwards it as a Vulkan debug label.
            commandList->BeginEvent(1, label, N - 1);
        }
    }
    Fh6GpuLabel(const Fh6GpuLabel &) = delete;
    ~Fh6GpuLabel()
    {
        if (commandList) commandList->EndEvent();
    }
};
