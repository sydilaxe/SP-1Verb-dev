/* SP-1 Reverb engine: an early-digital style plate/hall reverb for the Teenage Engineering SP-1.
 *
 * Eight models (TRACK buttons in the firmware), each its own structure:
 *   HALL     a large modulated tank: lush and long
 *   PLATE    dense and bright, builds fast
 *   ROOM     small and short
 *   SPRING   a 60s tube spring tank, Fisher Space Expander style: two springs, a long dispersive
 *            drip on every trip, thin lows, dark highs, a driven input, and a crash when a hard
 *            transient hits it (DIFFUSION: how much drip)
 *   CLOUD    huge, sparse, heavily modulated: a scattered ambient wash
 *   SHIMMER  an octave up in the tank's feedback: each echo climbs
 *   GATED    the 80s non-linear drum reverb: dense, then cut off (DECAY sets the gate time)
 *   REVERSE  swells up after each hit, then cuts off (DECAY sets the swell length)
 * Switching models fades the reverb out and in (about 20 ms each way); the dry never moves.
 *
 * Page 1 (the faders as they come up):
 *   fader 1  MIX        dry (bottom) to all reverb (top), constant power
 *   fader 2  DECAY      the reverb time (its range depends on the model); darker as it lengthens
 *   fader 3  DIFFUSION  scattered echoes (bottom) to a dense cloud (top)
 *   fader 4  FILTER     the reverb's tone: the middle fifth is flat; down sweeps a low-pass down,
 *                       up sweeps a high-pass up. The dry signal is never filtered.
 * Page 2 (FWD in the firmware):
 *   fader 1  MOD DEPTH  chorusing of the tank's delay lines
 *   fader 2  MOD RATE   0.2 to 4.5 Hz
 *   fader 3  PRE-DELAY  0 to 150 ms before the reverb starts
 *   fader 4  GRIT       early-digital converter: fewer bits (down to 6) on the reverb, following
 *                       its level so quiet tails are gritty too rather than cut off
 *
 * After the mix, an always-on safety ceiling (-1 dBFS, 1 ms look-ahead) that only acts on overs.
 *
 * Full bandwidth: everything runs at 48 kHz in 32-bit float. With MIX at the bottom the output is
 * the input, bit for bit, fx_latency() frames later.
 *
 * Float, stereo, any block size: parameters move every FX_CHUNK frames counted from the start, so
 * the output does not depend on how the audio is cut into blocks. No libm, no heap: an fx_engine
 * is about 130 KB, mostly delay lines, and should be a static. */
#ifndef SP1_REVERB_FX_H
#define SP1_REVERB_FX_H

#include <stdint.h>
#include "fx_math.h"

#define FX_SLOTS 4
#define FX_CHUNK 32   /* frames (48 kHz) between parameter updates: 0.67 ms */

enum { FX_MIX, FX_DECAY, FX_DIFFUSION, FX_FILTER };                  /* page 1 */
enum { FX_MOD_DEPTH, FX_MOD_RATE, FX_PREDELAY, FX_GRIT };             /* page 2 */
enum { FX_HALL, FX_PLATE, FX_ROOM, FX_SPRING, FX_CLOUD, FX_SHIMMER, FX_GATED, FX_REVERSE, FX_MODELS };

/* delay line lengths at 48 kHz (frames) */
#define PRE_MAX 7201                          /* 150 ms, plus one for the interpolation */
#define AP_IN1 773
#define AP_IN2 587
#define AP_IN3 409
#define AP_IN4 307
#define TK_AP1 1753
#define TK_D1  4241                           /* read at about 3900 + modulation */
#define TK_D2  3571
#define TK_AP2 1811
#define TK_D3  4691                           /* read at about 4360 + modulation */
#define TK_D4  3989
#define SH_LEN 2048    /* shimmer pitch shifter window */
#define SPR_N  36      /* allpasses in each spring's dispersion (at 12 kHz) */

#define LIM_LA 48      /* safety look-ahead: 1 ms, and its latency */
#define LIM_SEG 8
#define LIM_NSEG (LIM_LA / LIM_SEG)

typedef struct {
    float sr;
    int phase;                       /* frames into the current chunk */
    float fader[FX_SLOTS];           /* page 1 positions, 0..1 */
    float param2[FX_SLOTS];          /* page 2 positions, 0..1 */

    /* smoothed controls */
    float mix, decay, diffusion, depth, predelay, grit;
    float dry_g, wet_g, dry_t, wet_t, dry_step, wet_step;   /* ramped across each chunk */
    float fb, damp_c, diff_c;
    float lfo1, lfo2, lfo_inc;       /* triangle LFOs, phase 0..1 */

    /* the model, and its lengths and settings (set when it is switched in) */
    int model, model_next;
    float swg;                       /* the switch fade: 1 playing, 0 swapped out */
    float in_g, in_t, in_step;       /* the reverb's input, faded in after a switch */
    int n_ap[4], n_tap1, n_tap2, n_d2, n_d4, n_tap1x, n_tap2x;
    float size, tank_c, depth_scale;

    /* GATED / REVERSE: an envelope on the reverb, keyed by the input */
    float k_det, k_slow, k_env, k_att, k_rel, k_rel_s, k_close;
    int k_t, k_len, k_run;

    /* SHIMMER */
    float sh_buf[SH_LEN];
    int sh_w;
    float sh_ph;

    /* SPRING */
    float sp_d[2][SPR_N];           /* the dispersion's state */
    int sp_ph;                      /* the springs run at 12 kHz: sp_ph counts 48 kHz frames, 0..3 */
    fx_svf sp_f1;                   /* 4.5 kHz low-pass, then the average of 4: down to 12 kHz */
    fx_svf_state sp_in;
    float sp_acc;
    float sp_y[4];                  /* back up to 48 kHz: each spring's last two frames */
    float sp_hp[4], sp_hc, sp_oc;   /* the springs' low end: in the loop, and at the pickup */
    float sp_a;                     /* the dispersion's strength (DIFFUSION) */
    float sp_fast, sp_slow, sp_crash; /* a hard hit rattles the springs: the crash */
    uint32_t sp_rng;
    float sp_k[4];

    /* pre-delay and input diffusers */
    float pre[PRE_MAX];
    int pre_w;
    float ap1[AP_IN1], ap2[AP_IN2], ap3[AP_IN3], ap4[AP_IN4];
    int ap_i[4];

    /* the tank: two cross-coupled loops */
    float t_ap1[TK_AP1], t_d1[TK_D1], t_d2[TK_D2];
    float t_ap2[TK_AP2], t_d3[TK_D3], t_d4[TK_D4];
    int ti_ap1, ti_d1, ti_d2, ti_ap2, ti_d3, ti_d4;
    float damp_l, damp_r;

    /* the reverb's filter */
    float f_pos, f_gate, f_gate_target;
    int f_mode;                      /* 0 high-pass, 1 low-pass */
    fx_svf f_svf;
    fx_svf_state f_st[2];

    /* grit */
    float g_q, g_inv_q, g_env, g_rel, g_wet;

    /* safety ceiling */
    float ceiling;
    float dl[2][LIM_LA];
    float seg[LIM_NSEG];
    float acc_pk;
    int si, sn, pos_dl;
    float sgr, s_rel, sg, sg_step;
    float meter;                     /* deepest reduction since last read, 0..1 (1 - gain) */
} fx_engine;

void fx_init(fx_engine *e, float sr);
void fx_set_fader(fx_engine *e, int slot, float pos);     /* page 1, 0..1 */
void fx_set_param2(fx_engine *e, int slot, float pos);    /* page 2, 0..1 */
void fx_set_model(fx_engine *e, int model);               /* FX_HALL .. FX_REVERSE */
const char *fx_model_name(int model);
void fx_process(fx_engine *e, float *l, float *r, int n); /* in place */
int fx_latency(const fx_engine *e);                       /* frames */
float fx_gain_reduction_db(const fx_engine *e);           /* the safety ceiling, from the meter */
float fx_decay_seconds(int model, float pos);             /* the RT60 (or gate time) DECAY sets */

#endif
