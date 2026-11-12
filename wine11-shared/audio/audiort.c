/* SPDX-License-Identifier: GPL-3.0-only
 * Audio scheduling, buffering, and underrun de-popping for Black Myth: Wukong.
 * Based on Highball's spike/audiobuf/audiobuf.c
 * (upstream commit 8aacc6b0a682422939bbc663d0a83b2db3458431).
 */
#include <AudioToolbox/AudioToolbox.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <os/lock.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#define PROP_BUFFER_FRAME_SIZE 0x6673697au /* 'fsiz' */
#define MAX_TRACKED_STREAMS 16
#define MAX_PROMOTED_THREADS 64
#define MAX_DE_POP_CHANNELS 8
#define DEFAULT_MIN_RING_PERIODS 6u
#define DEFAULT_TIMER_PERIOD_HNS 25000 /* 2.5 ms in 100-ns units (4x catch-up) */
#define CROSSFADE_FRAMES 64u

/*
 * winecoreaudio.so render stream layout (x86_64):
 *   +0x00: os_unfair_lock lock
 *   +0x08: AudioUnit unit
 *   +0x18: AudioStreamBasicDescription dev_desc (40 bytes)
 *   +0x44: int32_t flow (0 = eRender, 1 = eCapture)
 *   +0x48: uint32_t flags (0x00040000 = AUDCLNT_STREAMFLAGS_EVENTCALLBACK)
 *   +0x4c: int32_t share (0 = shared, 1 = exclusive)
 *   +0x58: int32_t playing
 *   +0x60: int64_t period (REFERENCE_TIME, 100-ns units)
 *   +0x68: uint32_t period_frames
 *   +0x6c: uint32_t bufsize_frames
 *   +0x78: uint32_t held_frames
 *   +0xa8: void *fmt (WAVEFORMATEX *)
 *   +0xd8: AudioUnit spatial_unit
 */
struct stream_slot {
    void * _Atomic stream;
    AudioUnit unit;
    AURenderCallback orig_cb;
    void *orig_refcon;
    atomic_ullong render_cb_tid;
    int cb_snap_valid;
    int32_t cb_playing;
    uint32_t cb_held_frames;
    uint32_t period_frames;
    uint32_t channels;
    int is_float32_interleaved;
    int in_underrun;
    float last_sample[MAX_DE_POP_CHANNELS];
    uint64_t total_frames;
    uint64_t underrun_frames;
    uint32_t underrun_events;
};

static struct stream_slot g_slots[MAX_TRACKED_STREAMS];
static os_unfair_lock g_slots_lock = OS_UNFAIR_LOCK_INIT;
static atomic_int g_active_streams = 0;
static atomic_ullong g_promoted_threads[MAX_PROMOTED_THREADS];
static atomic_int g_enabled_state = -1;
static atomic_int g_debug_state = -1;

static uint64_t current_tid(void)
{
    uint64_t tid = 0;
    pthread_threadid_np(NULL, &tid);
    return tid ? tid : (uint64_t)(uintptr_t)pthread_self();
}

static int audiort_enabled(void)
{
    int state = atomic_load_explicit(&g_enabled_state, memory_order_relaxed);
    if (state < 0) {
        const char *env = getenv("HB_AUDIORT");
        state = !(env && env[0] == '0');
        atomic_store_explicit(&g_enabled_state, state, memory_order_relaxed);
    }
    return state;
}

static int debug_enabled(void)
{
    int state = atomic_load_explicit(&g_debug_state, memory_order_relaxed);
    if (state < 0) {
        const char *env = getenv("HB_AUDIOBUF_DEBUG");
        state = (env && env[0] != '\0' && env[0] != '0') ? 1 : 0;
        atomic_store_explicit(&g_debug_state, state, memory_order_relaxed);
    }
    return state;
}

static int is_thread_marked(uint64_t tid)
{
    for (int i = 0; i < MAX_PROMOTED_THREADS; ++i) {
        unsigned long long cur = atomic_load_explicit(&g_promoted_threads[i], memory_order_relaxed);
        if (cur == tid)
            return 1;
        if (cur == 0)
            return 0;
    }
    return 1;
}

static int mark_thread_promoted(uint64_t tid)
{
    for (int i = 0; i < MAX_PROMOTED_THREADS; ++i) {
        unsigned long long cur = atomic_load_explicit(&g_promoted_threads[i], memory_order_relaxed);
        if (cur == tid)
            return 0;
        if (cur == 0) {
            unsigned long long expected = 0;
            if (atomic_compare_exchange_strong_explicit(
                    &g_promoted_threads[i], &expected, tid,
                    memory_order_relaxed, memory_order_relaxed))
                return 1;
            if (expected == tid)
                return 0;
        }
    }
    return 0;
}

static void make_realtime(const char *label)
{
    uint64_t tid = current_tid();
    if (!mark_thread_promoted(tid))
        return;

    mach_timebase_info_data_t timebase;
    if (mach_timebase_info(&timebase) != KERN_SUCCESS || !timebase.numer)
        return;

    double ticks_per_ms = 1e6 * (double)timebase.denom / (double)timebase.numer;
    thread_time_constraint_policy_data_t policy = {
        .period = (uint32_t)(5.0 * ticks_per_ms),
        .computation = (uint32_t)(3.0 * ticks_per_ms),
        .constraint = (uint32_t)(5.0 * ticks_per_ms),
        .preemptible = 1,
    };
    kern_return_t status = thread_policy_set(
        pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
        (thread_policy_t)&policy, THREAD_TIME_CONSTRAINT_POLICY_COUNT);
    if (debug_enabled())
        fprintf(stderr, "audiort: %s real time, status %d\n",
                label ? label : "thread", status);
}

static int contains_nocase(const char *haystack, const char *needle)
{
    size_t nlen = strlen(needle);
    if (!nlen)
        return 1;
    for (; *haystack; ++haystack) {
        if (strncasecmp(haystack, needle, nlen) == 0)
            return 1;
    }
    return 0;
}

static int is_audio_thread_name(const char *name)
{
    if (!name || !name[0])
        return 0;
    if (strcmp(name, "audio_client_timer") == 0 ||
        strcmp(name, "wine_mmdevapi_main_loop") == 0)
        return 1;
    if (contains_nocase(name, "audio") ||
        contains_nocase(name, "wwise") ||
        contains_nocase(name, "ak::") ||
        contains_nocase(name, "faudio") ||
        contains_nocase(name, "wasapi") ||
        contains_nocase(name, "xaudio") ||
        contains_nocase(name, "bink"))
        return 1;
    return 0;
}

static int is_valid_wine_stream(void *ptr, AudioUnit unit)
{
    if (!ptr || ((uintptr_t)ptr & 0x7u) != 0)
        return 0;
    uint8_t *s = (uint8_t *)ptr;
    AudioUnit u0 = *(AudioUnit *)(s + 0x08);
    AudioUnit u1 = *(AudioUnit *)(s + 0xd8);
    if (unit && u0 != unit && u1 != unit)
        return 0;
    int32_t flow = *(int32_t *)(s + 0x44);
    int32_t share = *(int32_t *)(s + 0x4c);
    int64_t period = *(int64_t *)(s + 0x60);
    uint32_t period_frames = *(uint32_t *)(s + 0x68);
    uint32_t bufsize_frames = *(uint32_t *)(s + 0x6c);
    void *fmt = *(void **)(s + 0xa8);
    if (flow != 0 || (share != 0 && share != 1))
        return 0;
    if (period < 5000 || period > 1000000)
        return 0;
    if (period_frames < 32 || period_frames > 16384)
        return 0;
    if (bufsize_frames < period_frames || bufsize_frames > 524288)
        return 0;
    if (!fmt)
        return 0;
    return 1;
}

static void apply_fast_timer_period(uint8_t *s)
{
    int64_t cur = *(volatile int64_t *)(s + 0x60);
    if (cur > DEFAULT_TIMER_PERIOD_HNS && cur <= 1000000) {
        *(volatile int64_t *)(s + 0x60) = DEFAULT_TIMER_PERIOD_HNS;
        if (debug_enabled())
            fprintf(stderr, "audiort: stream %p timer period %lld -> %d (100ns)\n",
                    (void *)s, (long long)cur, DEFAULT_TIMER_PERIOD_HNS);
    }
}

static int audio_pthread_setname_np(const char *name)
{
    int result = pthread_setname_np(name);
    if (result == 0 && name && audiort_enabled()) {
        if (strcmp(name, "audio_client_timer") == 0) {
            os_unfair_lock_lock(&g_slots_lock);
            for (int i = 0; i < MAX_TRACKED_STREAMS; ++i) {
                void *s = atomic_load_explicit(&g_slots[i].stream, memory_order_relaxed);
                if (s && is_valid_wine_stream(s, NULL))
                    apply_fast_timer_period((uint8_t *)s);
            }
            os_unfair_lock_unlock(&g_slots_lock);
        }
        if (is_audio_thread_name(name))
            make_realtime(name);
    }
    return result;
}

static void audio_os_unfair_lock_lock(os_unfair_lock_t lock)
{
    os_unfair_lock_lock(lock);

    if (__builtin_expect(atomic_load_explicit(&g_active_streams, memory_order_relaxed) == 0, 1))
        return;

    struct stream_slot *matched_slot = NULL;
    for (int i = 0; i < MAX_TRACKED_STREAMS; ++i) {
        void *s = atomic_load_explicit(&g_slots[i].stream, memory_order_relaxed);
        if (s == (void *)lock) {
            matched_slot = &g_slots[i];
            break;
        }
    }
    if (__builtin_expect(matched_slot == NULL, 1))
        return;

    uint8_t *base = (uint8_t *)lock;
    uint64_t tid = current_tid();
    if (atomic_load_explicit(&matched_slot->render_cb_tid, memory_order_relaxed) == tid) {
        matched_slot->cb_playing = *(volatile int32_t *)(base + 0x58);
        matched_slot->cb_held_frames = *(volatile uint32_t *)(base + 0x78);
        matched_slot->cb_snap_valid = 1;
        if (*(volatile int64_t *)(base + 0x60) > DEFAULT_TIMER_PERIOD_HNS)
            *(volatile int64_t *)(base + 0x60) = DEFAULT_TIMER_PERIOD_HNS;
        return;
    }

    if (is_thread_marked(tid))
        return;

    if (!audiort_enabled()) {
        mark_thread_promoted(tid);
        return;
    }

    if (is_valid_wine_stream((void *)lock, NULL)) {
        apply_fast_timer_period(base);
        make_realtime("audio_render_thread");
    }
}

static OSStatus wukong_render_cb(
    void *inRefCon,
    AudioUnitRenderActionFlags *ioActionFlags,
    const AudioTimeStamp *inTimeStamp,
    UInt32 inBusNumber,
    UInt32 inNumberFrames,
    AudioBufferList *ioData)
{
    struct stream_slot *slot = (struct stream_slot *)inRefCon;
    if (!slot || !slot->orig_cb)
        return noErr;

    slot->cb_snap_valid = 0;
    atomic_store_explicit(&slot->render_cb_tid, current_tid(), memory_order_relaxed);

    OSStatus status = slot->orig_cb(
        slot->orig_refcon, ioActionFlags, inTimeStamp,
        inBusNumber, inNumberFrames, ioData);

    atomic_store_explicit(&slot->render_cb_tid, 0, memory_order_relaxed);

    if (status != noErr || !slot->cb_snap_valid || !ioData ||
        ioData->mNumberBuffers != 1 || !ioData->mBuffers[0].mData ||
        !slot->is_float32_interleaved || inNumberFrames == 0)
        return status;

    uint32_t ch_count = ioData->mBuffers[0].mNumberChannels;
    if (ch_count == 0)
        ch_count = slot->channels;
    if (ch_count == 0 || ch_count > MAX_DE_POP_CHANNELS)
        return status;

    float *buf = (float *)ioData->mBuffers[0].mData;
    uint32_t max_buf_frames = ioData->mBuffers[0].mDataByteSize / (ch_count * sizeof(float));
    if (max_buf_frames < inNumberFrames)
        inNumberFrames = max_buf_frames;
    if (inNumberFrames == 0)
        return status;

    if (!slot->cb_playing) {
        slot->in_underrun = 0;
        for (uint32_t c = 0; c < ch_count; ++c)
            slot->last_sample[c] = 0.0f;
        return status;
    }

    slot->total_frames += inNumberFrames;
    uint32_t valid_frames = slot->cb_held_frames < inNumberFrames
        ? slot->cb_held_frames
        : inNumberFrames;

    /* Smoothly ramp back in when recovering from an underrun. */
    if (slot->in_underrun && valid_frames > 0) {
        uint32_t ramp = valid_frames < CROSSFADE_FRAMES ? valid_frames : CROSSFADE_FRAMES;
        float inv_ramp = 1.0f / (float)(ramp + 1u);
        for (uint32_t f = 0; f < ramp; ++f) {
            float alpha = (float)(f + 1u) * inv_ramp;
            float one_minus = 1.0f - alpha;
            float *frame = buf + (size_t)f * ch_count;
            for (uint32_t c = 0; c < ch_count; ++c)
                frame[c] = slot->last_sample[c] * one_minus + frame[c] * alpha;
        }
        slot->in_underrun = 0;
    }

    if (valid_frames > 0) {
        const float *last = buf + (size_t)(valid_frames - 1u) * ch_count;
        for (uint32_t c = 0; c < ch_count; ++c)
            slot->last_sample[c] = last[c];
    }

    /* Replace hard zero-fill on underrun with an exponential decay fade-out. */
    if (valid_frames < inNumberFrames) {
        slot->in_underrun = 1;
        slot->underrun_events += 1;
        slot->underrun_frames += (inNumberFrames - valid_frames);
        for (uint32_t f = valid_frames; f < inNumberFrames; ++f) {
            float *frame = buf + (size_t)f * ch_count;
            for (uint32_t c = 0; c < ch_count; ++c) {
                slot->last_sample[c] *= 0.96f;
                if (slot->last_sample[c] > -1e-6f && slot->last_sample[c] < 1e-6f)
                    slot->last_sample[c] = 0.0f;
                frame[c] = slot->last_sample[c];
            }
        }
    }

    return status;
}

static OSStatus audio_AudioUnitSetProperty(
    AudioUnit inUnit,
    AudioUnitPropertyID inID,
    AudioUnitScope inScope,
    AudioUnitElement inElement,
    const void *inData,
    UInt32 inDataSize)
{
    if (!audiort_enabled())
        return AudioUnitSetProperty(inUnit, inID, inScope, inElement, inData, inDataSize);

    if (inID == kAudioUnitProperty_SetRenderCallback &&
        inScope == kAudioUnitScope_Input &&
        inData && inDataSize == sizeof(AURenderCallbackStruct)) {
        const AURenderCallbackStruct *cb = (const AURenderCallbackStruct *)inData;
        if (cb->inputProc && is_valid_wine_stream(cb->inputProcRefCon, inUnit)) {
            uint8_t *s = (uint8_t *)cb->inputProcRefCon;
            uint32_t period_frames = *(uint32_t *)(s + 0x68);
            uint32_t orig_bufsize = *(uint32_t *)(s + 0x6c);
            uint32_t min_bufsize = period_frames * DEFAULT_MIN_RING_PERIODS;
            if (orig_bufsize < min_bufsize) {
                *(uint32_t *)(s + 0x6c) = min_bufsize;
                if (debug_enabled())
                    fprintf(stderr, "audiort: stream %p ringbuffer %u -> %u frames\n",
                            (void *)s, orig_bufsize, min_bufsize);
            }

            AudioUnit primary_unit = *(AudioUnit *)(s + 0x08);
            if (primary_unit == inUnit) {
                UInt32 cur_fsiz = 0;
                UInt32 sz = sizeof(cur_fsiz);
                if (AudioUnitGetProperty(inUnit, PROP_BUFFER_FRAME_SIZE,
                                         kAudioUnitScope_Global, 0,
                                         &cur_fsiz, &sz) == noErr &&
                    cur_fsiz > 0 && cur_fsiz < period_frames) {
                    UInt32 target_fsiz = period_frames;
                    AudioUnitSetProperty(inUnit, PROP_BUFFER_FRAME_SIZE,
                                         kAudioUnitScope_Global, 0,
                                         &target_fsiz, sizeof(target_fsiz));
                }

                AudioStreamBasicDescription in_desc;
                UInt32 desc_sz = sizeof(in_desc);
                uint32_t fmt_id = *(uint32_t *)(s + 0x20);
                uint32_t fmt_flags = *(uint32_t *)(s + 0x24);
                uint32_t ch = *(uint32_t *)(s + 0x34);
                uint32_t bits = *(uint32_t *)(s + 0x38);
                if (AudioUnitGetProperty(inUnit, kAudioUnitProperty_StreamFormat,
                                         kAudioUnitScope_Input, 0,
                                         &in_desc, &desc_sz) == noErr &&
                    desc_sz == sizeof(in_desc)) {
                    fmt_id = in_desc.mFormatID;
                    fmt_flags = in_desc.mFormatFlags;
                    ch = in_desc.mChannelsPerFrame;
                    bits = in_desc.mBitsPerChannel;
                }
                int is_f32_interleaved =
                    (fmt_id == kAudioFormatLinearPCM) &&
                    ((fmt_flags & kAudioFormatFlagIsFloat) != 0) &&
                    ((fmt_flags & kAudioFormatFlagIsNonInterleaved) == 0) &&
                    (bits == 32) && (ch >= 1 && ch <= MAX_DE_POP_CHANNELS);

                os_unfair_lock_lock(&g_slots_lock);
                struct stream_slot *chosen = NULL;
                for (int i = 0; i < MAX_TRACKED_STREAMS; ++i) {
                    void *existing = atomic_load_explicit(&g_slots[i].stream, memory_order_relaxed);
                    if (existing == (void *)s || g_slots[i].unit == inUnit) {
                        chosen = &g_slots[i];
                        break;
                    }
                    if (!chosen && existing == NULL)
                        chosen = &g_slots[i];
                }
                if (!chosen)
                    chosen = &g_slots[0];

                memset(chosen, 0, sizeof(*chosen));
                chosen->unit = inUnit;
                chosen->orig_cb = cb->inputProc;
                chosen->orig_refcon = cb->inputProcRefCon;
                chosen->period_frames = period_frames;
                chosen->channels = ch;
                chosen->is_float32_interleaved = is_f32_interleaved;
                atomic_store_explicit(&chosen->stream, (void *)s, memory_order_release);
                atomic_store_explicit(&g_active_streams, 1, memory_order_release);
                os_unfair_lock_unlock(&g_slots_lock);

                AURenderCallbackStruct wrapped = {
                    .inputProc = wukong_render_cb,
                    .inputProcRefCon = chosen,
                };
                return AudioUnitSetProperty(
                    inUnit, inID, inScope, inElement, &wrapped, sizeof(wrapped));
            }
        }
    }

    if (inID == PROP_BUFFER_FRAME_SIZE &&
        inScope == kAudioUnitScope_Global &&
        inData && inDataSize == sizeof(UInt32)) {
        UInt32 req_frames = *(const UInt32 *)inData;
        UInt32 min_frames = 480;

        os_unfair_lock_lock(&g_slots_lock);
        for (int i = 0; i < MAX_TRACKED_STREAMS; ++i) {
            void *s = atomic_load_explicit(&g_slots[i].stream, memory_order_relaxed);
            if (s && g_slots[i].unit == inUnit) {
                if (g_slots[i].period_frames >= 128 && g_slots[i].period_frames <= 4096)
                    min_frames = g_slots[i].period_frames;
                if (is_valid_wine_stream(s, inUnit))
                    apply_fast_timer_period((uint8_t *)s);
                break;
            }
        }
        os_unfair_lock_unlock(&g_slots_lock);

        if (req_frames > 0 && req_frames < min_frames) {
            if (debug_enabled())
                fprintf(stderr, "audiort: unit %p clamping I/O buffer %u -> %u frames\n",
                        (void *)inUnit, (unsigned)req_frames, (unsigned)min_frames);
            return AudioUnitSetProperty(
                inUnit, inID, inScope, inElement, &min_frames, sizeof(min_frames));
        }
    }

    return AudioUnitSetProperty(inUnit, inID, inScope, inElement, inData, inDataSize);
}

static void ensure_unit_buffer_and_period(AudioUnit inUnit, const char *stage)
{
    if (!audiort_enabled())
        return;

    UInt32 min_frames = 0;
    os_unfair_lock_lock(&g_slots_lock);
    for (int i = 0; i < MAX_TRACKED_STREAMS; ++i) {
        void *s = atomic_load_explicit(&g_slots[i].stream, memory_order_relaxed);
        if (s && g_slots[i].unit == inUnit) {
            min_frames = (g_slots[i].period_frames >= 128 && g_slots[i].period_frames <= 4096)
                ? g_slots[i].period_frames
                : 480;
            if (is_valid_wine_stream(s, inUnit))
                apply_fast_timer_period((uint8_t *)s);
            break;
        }
    }
    os_unfair_lock_unlock(&g_slots_lock);

    if (min_frames > 0) {
        UInt32 cur_fsiz = 0;
        UInt32 sz = sizeof(cur_fsiz);
        if (AudioUnitGetProperty(inUnit, PROP_BUFFER_FRAME_SIZE,
                                 kAudioUnitScope_Global, 0,
                                 &cur_fsiz, &sz) == noErr &&
            cur_fsiz > 0 && cur_fsiz < min_frames) {
            OSStatus st = AudioUnitSetProperty(inUnit, PROP_BUFFER_FRAME_SIZE,
                                               kAudioUnitScope_Global, 0,
                                               &min_frames, sizeof(min_frames));
            if (debug_enabled())
                fprintf(stderr, "audiort: unit %p restoring I/O buffer %u -> %u frames on %s (status %d)\n",
                        (void *)inUnit, (unsigned)cur_fsiz, (unsigned)min_frames,
                        stage ? stage : "init", (int)st);
        }
    }
}

static OSStatus audio_AudioUnitInitialize(AudioUnit inUnit)
{
    ensure_unit_buffer_and_period(inUnit, "pre-init");
    OSStatus status = AudioUnitInitialize(inUnit);
    if (status == noErr)
        ensure_unit_buffer_and_period(inUnit, "post-init");
    return status;
}

static OSStatus audio_AudioOutputUnitStart(AudioUnit ci)
{
    ensure_unit_buffer_and_period(ci, "start");
    return AudioOutputUnitStart(ci);
}

static OSStatus audio_AudioComponentInstanceDispose(AudioComponentInstance inInstance)
{
    os_unfair_lock_lock(&g_slots_lock);
    for (int i = 0; i < MAX_TRACKED_STREAMS; ++i) {
        if (g_slots[i].unit == (AudioUnit)inInstance) {
            if (debug_enabled() && g_slots[i].total_frames > 0) {
                fprintf(stderr,
                        "audiort: unit %p disposed, %llu frames rendered, %u underrun events (%llu frames smoothed)\n",
                        (void *)inInstance,
                        (unsigned long long)g_slots[i].total_frames,
                        g_slots[i].underrun_events,
                        (unsigned long long)g_slots[i].underrun_frames);
            }
            atomic_store_explicit(&g_slots[i].stream, NULL, memory_order_release);
            g_slots[i].unit = NULL;
        }
    }
    os_unfair_lock_unlock(&g_slots_lock);
    return AudioComponentInstanceDispose(inInstance);
}

__attribute__((used, section("__DATA,__interpose")))
static const struct {
    const void *replacement;
    const void *original;
} interposers[] = {
    { (const void *)audio_pthread_setname_np, (const void *)pthread_setname_np },
    { (const void *)audio_os_unfair_lock_lock, (const void *)os_unfair_lock_lock },
    { (const void *)audio_AudioUnitSetProperty, (const void *)AudioUnitSetProperty },
    { (const void *)audio_AudioUnitInitialize, (const void *)AudioUnitInitialize },
    { (const void *)audio_AudioOutputUnitStart, (const void *)AudioOutputUnitStart },
    { (const void *)audio_AudioComponentInstanceDispose, (const void *)AudioComponentInstanceDispose },
};

