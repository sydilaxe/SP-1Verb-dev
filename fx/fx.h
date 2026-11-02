/* SP-1 Reverb Engine: Dedicated Early Digital Reverb for Teenage Engineering SP-1.
 *
 * Controls (Bank 1):
 *   Fader 1 (0): MIX        Reverb dry/wet mix (0.0 = 100% dry, 1.0 = 100% wet)
 *   Fader 2 (1): DECAY      Decay time (0.2s to 20s+ / infinite wash)
 *   Fader 3 (2): DIFFUSION  Early reflection diffusion & tank density
 *   Fader 4 (3): FILTER     Bi-directional HP/LP: centre fifth (0.4-0.6) flat,
 *                           down sweeps 2-pole low-pass down,
 *                           up sweeps 2-pole high-pass up.
 *
 * Controls (Bank 2 - toggled via PLAY button):
 *   Fader 1 (0): MOD DEPTH  Chorus/pitch flutter in delay tanks (0..1)
 *   Fader 2 (1): MOD RATE   Modulation LFO speed (0.1 Hz to 5.0 Hz)
 *   Fader 3 (2): PRE-DELAY  0 ms to 150 ms pre-delay
 *   Fader 4 (3): GRIT       Early digital 12-bit/8-bit DAC decimation & transformer warmth
 *
 * Audio path:
 *   In and out at 48 kHz stereo (USB Audio Class 2 + CS42L42 / TAS2505 codecs).
 *   Input warm stage -> polyphase IIR half-band decimation to 24 kHz (cuts CPU in half,
 *   doubles maximum reverb delay time, authentic early digital bandwidth).
 *   Stereo Schroeder-Dattorro early digital tank with 4 input allpass diffusers,
 *   dual cross-coupled modulated delay loops, 1-pole damping filters, SVF tone shaping.
 *   Polyphase IIR half-band interpolation back to 48 kHz.
 *   Transparent mastering ceiling at -0.3 dBFS.
 *
 * Float, stereo, any block size, zero heap allocations, no libm dependencies.
 */

#ifndef SP1_REVERB_FX_H
#define SP1_REVERB_FX_H

#include <stdint.h>
#include "fx_math.h"

#define FX_SLOTS 4
#define FX_CHUNK 16   /* Core frames (24 kHz) between parameter ramps */

enum {
    FX_MIX = 0,
    FX_DECAY = 1,
    FX_DIFFUSION = 2,
    FX_FILTER = 3,
    /* Legacy aliases for compatibility with SP-1FX tools and tests */
    FX_DUB = 0,
    FX_REVERB = 1,
    FX_CRUSH = 2,
    FX_LIMITER = 3
};

#define PRE_DELAY_MAX 3600  /* 150 ms at 24 kHz */
#define DIFFUSER_STAGES 4

/* Allpass filter stage */
typedef struct {
    float *buf;
    int len;
    int idx;
    float coeff;
} fx_allpass;

/* Modulated delay line */
typedef struct {
    float *buf;
    int len;
    int idx;
} fx_delay;

/* Polyphase halfband filter states (10 coefficients) */
#define HB_C 10

typedef struct {
    float sr, cr;             /* outside rate (48 kHz), core rate (24 kHz) */
    int phase;                /* core frames into the current chunk */

    /* Fader values (0..1) */
    float fader[FX_SLOTS];
    float bank2[FX_SLOTS];
    int active_bank;          /* 0 = Main Bank, 1 = Mod/Character Bank */

    /* Input warm saturation & downsampling (48k -> 24k) */
    float warm_lp[2], warm_c;
    float dy[2][HB_C], dx0[2], dx1[2], dfirst[2];
    int dodd;

    /* Pre-delay line */
    float pre_buf[PRE_DELAY_MAX];
    int pre_wpos;
    int pre_len_target, pre_len;

    /* 4-stage input allpass diffusion */
    float ap_in_buf1[389];
    float ap_in_buf2[293];
    float ap_in_buf3[203];
    float ap_in_buf4[151];
    int ap_in_idx[4];
    float diff_coeff;

    /* Tank delay buffers (Left and Right cross-coupled loops) */
    float tank_d1_buf[2111];
    float tank_ap1_buf[877];
    float tank_d2_buf[1789];
    float tank_d3_buf[2341];
    float tank_ap2_buf[907];
    float tank_d4_buf[1993];

    int tank_d1_idx;
    int tank_ap1_idx;
    int tank_d2_idx;
    int tank_d3_idx;
    int tank_ap2_idx;
    int tank_d4_idx;

    /* Damping 1-pole low-pass filters in tanks */
    float damp_l, damp_r;
    float damp_coeff;

    /* LFO for tank modulation */
    float lfo_phase1, lfo_phase2;
    float lfo_inc;
    float mod_depth;

    /* Bi-directional SVF filter */
    float filter_pos;
    int filter_mode;          /* 0 = flat, 1 = lowpass, 2 = highpass */
    fx_svf svf_l, svf_r;
    fx_svf_state svf_st_l, svf_st_r;

    /* Vintage DAC quantization (Grit) */
    float grit_amt;
    float grit_step;
    float inv_grit_step;

    /* Decay feedback gain */
    float decay_gain;
    float mix_dry, mix_wet;
    float prev_tail_energy;

    /* Upsampling (24k -> 48k) */
    float uy[2][HB_C], ux0[2], ux1[2], usecond[2];
    int uodd;

    /* Lookahead safety ceiling at -0.3 dBFS */
    float lim_buf[2][24];
    int lim_pos;

    /* Hardware metering & test compatibility */
    struct {
        float meter;
    } limiter;
    struct {
        int dust_on;
    } dub;
} fx_engine;

void fx_init(fx_engine *e, float sr);
void fx_set_fader(fx_engine *e, int slot, float pos);      /* pos 0..1 */
void fx_set_bank2(fx_engine *e, int slot, float pos);      /* pos 0..1 */
void fx_set_bank(fx_engine *e, int bank);                  /* 0 or 1 */
void fx_process(fx_engine *e, float *l, float *r, int n);  /* in place */
int fx_latency(const fx_engine *e);                        /* frames */

#endif /* SP1_REVERB_FX_H */
