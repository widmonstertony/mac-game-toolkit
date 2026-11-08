#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <mmdeviceapi.h>
#include <audioclient.h>
#include <ks.h>
#include <ksmedia.h>
#include <stdio.h>
#include <math.h>

#define CHECK(expr) do { hr=(expr); if (FAILED(hr)) { printf("%s: %#lx\n", #expr, (unsigned long)hr); return 1; } } while (0)
int main(void)
{
    HRESULT hr;
    IMMDeviceEnumerator *enumerator;
    IMMDevice *endpoint;
    IAudioClient *client;
    IAudioRenderClient *render;
    WAVEFORMATEXTENSIBLE format = {0};
    UINT32 capacity, padding;
    unsigned frame = 0;
    const unsigned channels = 12, sample_rate = 48000;

    CHECK(CoInitializeEx(NULL, COINIT_MULTITHREADED));
    CHECK(CoCreateInstance(&CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, &IID_IMMDeviceEnumerator, (void **)&enumerator));
    CHECK(IMMDeviceEnumerator_GetDefaultAudioEndpoint(enumerator, eRender, eConsole, &endpoint));
    CHECK(IMMDevice_Activate(endpoint, &IID_IAudioClient, CLSCTX_ALL, NULL, (void **)&client));
    format.Format.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    format.Format.nChannels = channels;
    format.Format.nSamplesPerSec = sample_rate;
    format.Format.nBlockAlign = channels * sizeof(float);
    format.Format.nAvgBytesPerSec = sample_rate * format.Format.nBlockAlign;
    format.Format.wBitsPerSample = 32;
    format.Format.cbSize = sizeof(format) - sizeof(format.Format);
    format.Samples.wValidBitsPerSample = 32;
    format.dwChannelMask = 0x2d63f;
    format.SubFormat = (GUID){3,0,0x0010,{0x80,0,0,0xaa,0,0x38,0x9b,0x71}};
    CHECK(IAudioClient_Initialize(client, AUDCLNT_SHAREMODE_SHARED,
          AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM, 1000000, 0, &format.Format, NULL));
    CHECK(IAudioClient_GetBufferSize(client, &capacity));
    CHECK(IAudioClient_GetService(client, &IID_IAudioRenderClient, (void **)&render));
    CHECK(IAudioClient_Start(client));
    while (frame < sample_rate / 2) {
        BYTE *raw;
        CHECK(IAudioClient_GetCurrentPadding(client, &padding));
        unsigned count = capacity - padding;
        if (count > sample_rate / 2 - frame) count = sample_rate / 2 - frame;
        if (!count) { Sleep(3); continue; }
        CHECK(IAudioRenderClient_GetBuffer(render, count, &raw));
        float *samples = (float *)raw;
        for (unsigned i = 0; i < count; ++i) {
            for (unsigned c = 0; c < channels; ++c) samples[i*channels+c] = 0;
            samples[i*channels+2] = 0.02f * sinf((float)(i+frame) * 0.057595865f);
            samples[i*channels+6] = 0.02f * sinf((float)(i+frame) * 0.086393798f);
        }
        CHECK(IAudioRenderClient_ReleaseBuffer(render, count, 0));
        frame += count;
    }
    Sleep(150);
    CHECK(IAudioClient_Stop(client));
    printf("Submitted %u frames with center and side channels only; front L/R were silent\n",frame);
    IAudioRenderClient_Release(render);
    IAudioClient_Release(client);
    IMMDevice_Release(endpoint);
    IMMDeviceEnumerator_Release(enumerator);
    CoUninitialize();
    return 0;
}
