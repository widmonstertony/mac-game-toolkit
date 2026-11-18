// Replay private game captures through the same GPU pass without launching FH6.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d12.h>
#include <cstdio>
#include <cstring>
#include "chain.h"
#include "../../store/metalfx-bridge/apple-visibility.h"

#define CHECK(x) do { if (!(x)) { printf("FAIL line=%d %s (%s)\n", __LINE__, #x, visibility.reason); return 1; } } while (0)
static constexpr UINT W = 128, H = 96;
#include "probe-gpu.h"
#include "replay-input.h"

int wmain(int argc, wchar_t **argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    AppleVisibility visibility;
    CHECK(argc == 4);
    Input old{}, current{};
    CHECK(load(argv[1], old) && load(argv[2], current));
    auto h = current.h;
    CHECK(old.h.rw == h.rw && old.h.rh == h.rh && old.h.w == h.w && old.h.h == h.h);
    auto generated = pixels(argv[2], L"generated-before-ui.bin", h.w, h.h, h.pitch[5]);
    CHECK(generated);
    GPU gpu;
    AppleFrameInputs inputs;
    CHECK(gpu.init() && inputs.initialize(gpu.d, gpu.q));
    Texture depth, motion, scene, output[4];
    CHECK(depth.init(gpu.d, DXGI_FORMAT_R32G8X24_TYPELESS, D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL, h.rw, h.rh));
    CHECK(motion.init(gpu.d, DXGI_FORMAT_R16G16_FLOAT, D3D12_RESOURCE_FLAG_NONE, h.rw, h.rh));
    CHECK(scene.init(gpu.d, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_NONE, h.w, h.h));
    for (auto &texture : output)
        CHECK(texture.init(gpu.d, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, h.w, h.h));
    ID3D12QueryHeap *query{};
    D3D12_QUERY_HEAP_DESC queryDesc{};
    queryDesc.Type = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
    queryDesc.Count = 2;
    CHECK(SUCCEEDED(gpu.d->CreateQueryHeap(&queryDesc, IID_ID3D12QueryHeap, (void **)&query)));
    ID3D12Resource *timing{};
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = 16;
    buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_READBACK;
    CHECK(SUCCEEDED(gpu.d->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_ID3D12Resource, (void **)&timing)));

    constexpr unsigned frameCount = 18;
    constexpr auto shaderRead = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
    for (unsigned i = 0; i < frameCount; ++i) {
        auto &sample = i % 2 ? current : old;
        auto &out = output[i % 4];
        CHECK(inputs.retireReusedOutput(out.gpu, i) && gpu.begin());
        CHECK(depth.write(gpu.l, sample.depth, h.rw * 4, shaderRead));
        CHECK(motion.write(gpu.l, sample.motion, h.rw * 4, shaderRead));
        CHECK(scene.write(gpu.l, current.scene, h.w * 4, shaderRead));
        CHECK(out.write(gpu.l, generated, h.w * 4, D3D12_RESOURCE_STATE_UNORDERED_ACCESS));
        auto frame = sample.frame;
        frame.frame = i;
        frame.reset = i == 0;
        auto slot = inputs.prepare(gpu.l, frame, depth.gpu, motion.gpu, h.rw, h.rh, shaderRead, shaderRead);
        CHECK(slot);
        const bool measure = i == frameCount - 1;
        if (measure) {
            slot->frame.reset = 1;
            CHECK(!visibility.record(inputs, *slot, gpu.l, scene.gpu, out.gpu));
            slot->frame.reset = 0;
            gpu.l->EndQuery(query, D3D12_QUERY_TYPE_TIMESTAMP, 0);
        }
        bool repaired = visibility.record(inputs, *slot, gpu.l, scene.gpu, out.gpu);
        CHECK(repaired == (i != 0));
        if (measure) {
            gpu.l->EndQuery(query, D3D12_QUERY_TYPE_TIMESTAMP, 1);
            gpu.l->ResolveQueryData(query, D3D12_QUERY_TYPE_TIMESTAMP, 0, 2, timing, 0);
            out.copyOut(gpu.l);
        }
        CHECK(inputs.recorded(slot, out.gpu) && gpu.end());
    }
    auto &out = output[(frameCount - 1) % 4];
    const DWORD bytes = h.w * h.h * 4;
    auto data = (BYTE *)HeapAlloc(GetProcessHeap(), 0, bytes);
    CHECK(data && out.read(data, h.w * 4));
    auto file = open(argv[3], L"corrected.bin", GENERIC_WRITE, CREATE_ALWAYS);
    DWORD written{};
    CHECK(file != INVALID_HANDLE_VALUE && WriteFile(file, data, bytes, &written, nullptr) && written == bytes);
    CloseHandle(file);
    unsigned changed{};
    for (DWORD i = 0; i < bytes; i += 4) {
        CHECK(generated[i + 3] == data[i + 3]);
        changed += memcmp(generated + i, data + i, 3) != 0;
    }
    UINT64 *ticks{}, frequency{};
    D3D12_RANGE range{0, 16};
    CHECK(SUCCEEDED(timing->Map(0, &range, (void **)&ticks)) &&
          SUCCEEDED(gpu.q->GetTimestampFrequency(&frequency)) && frequency);
    printf("PASS frames=%u changed=%u unchanged=%u alpha=unchanged GPU post ms=%.4f\n",
        frameCount, changed, h.w * h.h - changed, double(ticks[1] - ticks[0]) * 1000 / frequency);
    CHECK(changed > 0 && changed < h.w * h.h / 10);
    for (auto &target : output) CHECK(inputs.retireReusedOutput(target.gpu, frameCount));
    CHECK(gpu.begin() && gpu.end());
    inputs.poll();
    for (const auto &slot : inputs.slots)
        CHECK(slot.readers == 0 && !slot.previousRead && (&slot == inputs.latest || slot.stage == AppleFrameInputs::Free));
    inputs.releaseAfterGpuCompletion();
    visibility.release();
    return 0;
}
