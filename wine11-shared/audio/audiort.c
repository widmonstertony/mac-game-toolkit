/* SPDX-License-Identifier: GPL-3.0-only
 * Audio clock scheduling from Highball's spike/audiobuf/audiobuf.c.
 * Upstream commit 8aacc6b0a682422939bbc663d0a83b2db3458431.
 * https://github.com/gauthierpiarrette/highball
 *
 * This component keeps the existing engine's buffer and limiter callbacks.
 * It applies the upstream real-time policy only to Wine's audio clock thread.
 */
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void make_realtime(void)
{
    mach_timebase_info_data_t timebase;
    if (mach_timebase_info(&timebase) != KERN_SUCCESS || !timebase.numer)
        return;

    double ticks_per_ms = 1e6 * timebase.denom / timebase.numer;
    thread_time_constraint_policy_data_t policy = {
        .period = (uint32_t)(10 * ticks_per_ms),
        .computation = (uint32_t)(5 * ticks_per_ms),
        .constraint = (uint32_t)(10 * ticks_per_ms),
        .preemptible = 1,
    };
    kern_return_t status = thread_policy_set(
        pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
        (thread_policy_t)&policy, THREAD_TIME_CONSTRAINT_POLICY_COUNT);
    if (getenv("HB_AUDIOBUF_DEBUG"))
        fprintf(stderr, "audiort: audio_client_timer real time, status %d\n", status);
}

static int audio_pthread_setname_np(const char *name)
{
    int result = pthread_setname_np(name);
    const char *disabled = getenv("HB_AUDIORT");
    if (result == 0 && name && strcmp(name, "audio_client_timer") == 0 &&
        !(disabled && disabled[0] == '0'))
        make_realtime();
    return result;
}

__attribute__((used, section("__DATA,__interpose")))
static const struct {
    const void *replacement;
    const void *original;
} interposer = {
    (const void *)audio_pthread_setname_np,
    (const void *)pthread_setname_np,
};
