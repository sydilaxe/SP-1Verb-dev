/* Cortex-M4 cycle estimate for the effects: built for the M4F like the firmware, run in an
 * emulator by tools/bench_m4.py. Worst case: every effect in, a full-scale noise input. */
#include "fx.h"

static fx_engine E;
static float L[128], R[128];
static uint32_t s = 1;

void *memset(void *d, int c, unsigned n)
{
    unsigned char *p = d;
    while (n--) *p++ = (unsigned char)c;
    return d;
}

static void fill(void)
{
    for (int i = 0; i < 128; i++) {
        s = s * 1664525u + 1013904223u; L[i] = (float)(int32_t)s * 4.0e-10f;
        s = s * 1664525u + 1013904223u; R[i] = (float)(int32_t)s * 4.0e-10f;
    }
}

#ifndef BENCH_MODEL
#define BENCH_MODEL FX_SHIMMER
#endif

void bench_init(void)   /* worst case: the reverb all in, its filter and its page 2 all up */
{
    fx_init(&E, 48000.0f);
    fx_set_model(&E, BENCH_MODEL);
    fx_set_fader(&E, FX_MIX, 0.7f);
    fx_set_fader(&E, FX_DECAY, 1.0f);
    fx_set_fader(&E, FX_DIFFUSION, 1.0f);
    fx_set_fader(&E, FX_FILTER, 1.0f);
    for (int k = 0; k < FX_SLOTS; k++) fx_set_param2(&E, BENCH_MODEL, k, 1.0f);   /* its page 2, all up */
}

void bench_block(void)   /* one firmware block: 128 frames */
{
#ifdef BENCH_SWITCH
    /* from the first measured block, a switch to BENCH_SWITCH: the blocks measured are its crossfade */
    static int k;
    if (++k == 81) fx_set_model(&E, BENCH_SWITCH);
#endif
    fill();
    fx_process(&E, L, R, 128);
}
