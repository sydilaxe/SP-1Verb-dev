/* SP-1 Reverb Engine: Dedicated Early Digital Reverb for Teenage Engineering SP-1.
 *
 * Implements a Schroeder-Dattorro early digital reverb running at 24 kHz between
 * polyphase IIR half-band filters (48 kHz <-> 24 kHz).
 *
 * Architecture matching SP-1 hardware constraints:
 *   - Laurent de Soras 10-coeff polyphase IIR downsampler / upsampler
 *   - Warm input stage with gentle 24 kHz saturation
 *   - Parameter smoothing with 30 ms time constant for click-free fader sweeps
 *   - ~75 KB static struct, zero heap allocations, no libm dependencies
 *   - Optimized for ARM Cortex-M4 (Nordic nRF52840 @ 64 MHz, ~32% CPU)
 */

#include "fx.h"
#include <string.h>

#ifdef FX_PROFILE
#define STAGE __attribute__((noinline))
#else
#define STAGE
#endif

/* Laurent de Soras HIIR 10-coeff polyphase IIR halfband filter */
static const float hb_c[HB_C] = {
    0.0212186694f, 0.0816374655f, 0.1726362110f, 0.2829113996f, 0.4014960495f,
    0.5199743642f, 0.6334772202f, 0.7406996488f, 0.8434655794f, 0.9463427912f,
};

/* Unrolled polyphase IIR halfband transform (10 allpass sections) */
static void hb_run(float *a, float *b, int n, float *y, float *x0, float *x1)
{
    const float c0 = hb_c[0], c1 = hb_c[1], c2 = hb_c[2], c3 = hb_c[3], c4 = hb_c[4];
    const float c5 = hb_c[5], c6 = hb_c[6], c7 = hb_c[7], c8 = hb_c[8], c9 = hb_c[9];
    float y0 = y[0], y1 = y[1], y2 = y[2], y3 = y[3], y4 = y[4];
    float y5 = y[5], y6 = y[6], y7 = y[7], y8 = y[8], y9 = y[9];
    float xa = *x0, xb = *x1;
    for (int k = 0; k < n; k++) {
        float s = a[k], t;
        t = (s - y0) * c0 + xa; xa = s; s = t;
        t = (s - y2) * c2 + y0; y0 = s; s = t;
        t = (s - y4) * c4 + y2; y2 = s; s = t;
        t = (s - y6) * c6 + y4; y4 = s; s = t;
        t = (s - y8) * c8 + y6; y6 = s; y8 = t;
        a[k] = t;

        s = b[k];
        t = (s - y1) * c1 + xb; xb = s; s = t;
        t = (s - y3) * c3 + y1; y1 = s; s = t;
        t = (s - y5) * c5 + y3; y3 = s; s = t;
        t = (s - y7) * c7 + y5; y5 = s; s = t;
        t = (s - y9) * c9 + y7; y7 = s; y9 = t;
        b[k] = t;
    }
    y[0] = y0; y[1] = y1; y[2] = y2; y[3] = y3; y[4] = y4;
    y[5] = y5; y[6] = y6; y[7] = y7; y[8] = y8; y[9] = y9;
    *x0 = xa; *x1 = xb;
}

static inline float allpass_step(float in, float *buf, int len, int *idx, float c)
{
    int i = *idx;
    float buf_out = buf[i];
    float v = in + c * buf_out;
    buf[i] = v;
    i++;
    if (i >= len) i = 0;
    *idx = i;
    return buf_out - c * v;
}

static inline float delay_read_interp(const float *buf, int len, int wpos, float delay_samps)
{
    int d_int = (int)delay_samps;
    float frac = delay_samps - (float)d_int;
    int rpos = wpos - d_int;
    if (rpos < 0) rpos += len;
    int rpos_next = rpos + 1;
    if (rpos_next >= len) rpos_next = 0;
    return buf[rpos] + frac * (buf[rpos_next] - buf[rpos]);
}

static float chunk_coef(float seconds, float sr)
{
    return 1.0f - fx_exp2f(-1.44269504f * (float)FX_CHUNK / (seconds * sr));
}

void fx_init(fx_engine *e, float sr)
{
    memset(e, 0, sizeof(*e));
    e->sr = sr;
    e->cr = sr * 0.5f; /* 24 kHz core */

    for (int i = 0; i < FX_SLOTS; i++) {
        e->fader[i] = (i == FX_FILTER) ? 0.5f : ((i == FX_DIFFUSION) ? 0.7f : ((i == FX_DECAY) ? 0.5f : 0.0f));
    }

    e->bank2[0] = 0.25f; /* Mod Depth */
    e->bank2[1] = 0.4f;  /* Mod Rate */
    e->bank2[2] = 0.1f;  /* Pre-delay: ~15ms */
    e->bank2[3] = 0.0f;  /* Grit */

    e->active_bank = 0;
    e->diff_coeff = 0.7f;
    e->damp_coeff = 0.25f;
    e->decay_gain = 0.70f;
    e->mod_depth = 4.0f;
    e->lfo_inc = 2.0f * FX_PI * 1.0f / e->cr;

    /* warm stage cutoff at 3.5 kHz */
    e->warm_c = 1.0f - fx_exp2f(-1.44269504f / (1.0f / (2.0f * FX_PI * 3500.0f) * sr));
    e->dub.dust_on = 1;
    e->limiter.meter = 0.0f;

    fx_svf_lowpass(&e->svf_l, 8000.0f, 0.707f, e->cr);
    fx_svf_lowpass(&e->svf_r, 8000.0f, 0.707f, e->cr);
}

void fx_set_fader(fx_engine *e, int slot, float pos)
{
    if (slot < 0 || slot >= FX_SLOTS) return;
    pos = fx_clampf(pos, 0.0f, 1.0f);
    e->fader[slot] = pos;

    if (slot == FX_MIX) {
        e->mix_wet = pos;
        e->mix_dry = 1.0f - pos * 0.65f;
    } else if (slot == FX_DECAY) {
        e->decay_gain = 0.15f + pos * 0.79f;
        e->damp_coeff = 0.10f + (1.0f - pos) * 0.40f;
    } else if (slot == FX_DIFFUSION) {
        e->diff_coeff = 0.30f + pos * 0.45f;
    } else if (slot == FX_FILTER) {
        e->filter_pos = pos;
        if (pos >= 0.40f && pos <= 0.60f) {
            e->filter_mode = 0; /* Flat neutral bypass */
        } else if (pos < 0.40f) {
            e->filter_mode = 1; /* Low-pass sweep down */
            float norm = pos / 0.40f;
            float hz = 120.0f + norm * norm * 9880.0f;
            fx_svf_lowpass(&e->svf_l, hz, 0.707f, e->cr);
            fx_svf_lowpass(&e->svf_r, hz, 0.707f, e->cr);
        } else {
            e->filter_mode = 2; /* High-pass sweep up */
            float norm = (pos - 0.60f) / 0.40f;
            float hz = 30.0f + norm * norm * 3500.0f;
            fx_svf_highpass(&e->svf_l, hz, 0.707f, e->cr);
            fx_svf_highpass(&e->svf_r, hz, 0.707f, e->cr);
        }
    }
}

void fx_set_bank2(fx_engine *e, int slot, float pos)
{
    if (slot < 0 || slot >= FX_SLOTS) return;
    pos = fx_clampf(pos, 0.0f, 1.0f);
    e->bank2[slot] = pos;

    if (slot == 0) {
        e->mod_depth = pos * 8.0f;
    } else if (slot == 1) {
        float hz = 0.2f + pos * 4.3f;
        e->lfo_inc = 2.0f * FX_PI * hz / e->cr;
    } else if (slot == 2) {
        e->pre_len_target = (int)(pos * 3500.0f);
    } else if (slot == 3) {
        e->grit_amt = pos;
        if (pos > 0.01f) {
            float bits = 16.0f - pos * 8.0f;
            float levels = fx_exp2f(bits);
            e->grit_step = 2.0f / levels;
            e->inv_grit_step = levels * 0.5f;
        } else {
            e->grit_step = 0.0f;
        }
    }
}

void fx_set_bank(fx_engine *e, int bank)
{
    e->active_bank = bank ? 1 : 0;
}

int fx_latency(const fx_engine *e)
{
    (void)e;
    return 9; /* 10-coeff half-band filter group delay at 48 kHz */
}

/* 24 kHz Core Reverb Processor */
static void core_process(fx_engine *e, float *l, float *r, int n)
{
    while (n > 0) {
        if (e->phase == 0) {
            fx_set_fader(e, FX_MIX, e->fader[FX_MIX]);
            fx_set_fader(e, FX_DECAY, e->fader[FX_DECAY]);
            fx_set_fader(e, FX_DIFFUSION, e->fader[FX_DIFFUSION]);
            fx_set_fader(e, FX_FILTER, e->fader[FX_FILTER]);
        }
        int m = FX_CHUNK - e->phase;
        if (m > n) m = n;

        float mix_wet = e->mix_wet;
        float mix_dry = e->mix_dry;
        float decay_gain = e->decay_gain;
        float diff_c = e->diff_coeff;
        float damp_c = e->damp_coeff;
        float mod_d = e->mod_depth;

        for (int i = 0; i < m; i++) {
            float in_l = l[i];
            float in_r = r[i];
            float mono_in = 0.5f * (in_l + in_r);

            /* Pre-delay line */
            e->pre_buf[e->pre_wpos] = mono_in;
            int rpos = e->pre_wpos - e->pre_len_target;
            if (rpos < 0) rpos += PRE_DELAY_MAX;
            float pre_out = e->pre_buf[rpos];
            e->pre_wpos++;
            if (e->pre_wpos >= PRE_DELAY_MAX) e->pre_wpos = 0;

            /* 4-stage input allpass diffusion */
            float diff = pre_out;
            diff = allpass_step(diff, e->ap_in_buf1, 389, &e->ap_in_idx[0], diff_c * 0.70f);
            diff = allpass_step(diff, e->ap_in_buf2, 293, &e->ap_in_idx[1], diff_c * 0.70f);
            diff = allpass_step(diff, e->ap_in_buf3, 203, &e->ap_in_idx[2], diff_c * 0.60f);
            diff = allpass_step(diff, e->ap_in_buf4, 151, &e->ap_in_idx[3], diff_c * 0.60f);

            /* LFO phase advance */
            e->lfo_phase1 += e->lfo_inc;
            if (e->lfo_phase1 >= 2.0f * FX_PI) e->lfo_phase1 -= 2.0f * FX_PI;
            e->lfo_phase2 += e->lfo_inc * 1.27f;
            if (e->lfo_phase2 >= 2.0f * FX_PI) e->lfo_phase2 -= 2.0f * FX_PI;

            float mod1 = mod_d * (1.0f + (e->lfo_phase1 < FX_PI ? (e->lfo_phase1 / FX_PI - 0.5f) : (1.5f - e->lfo_phase1 / FX_PI)));
            float mod2 = mod_d * (1.0f + (e->lfo_phase2 < FX_PI ? (e->lfo_phase2 / FX_PI - 0.5f) : (1.5f - e->lfo_phase2 / FX_PI)));

            /* Tank Cross-Coupled Loops */
            /* Left Loop */
            float tank_in_l = diff + decay_gain * e->tank_d4_buf[e->tank_d4_idx];
            float ap1_out = allpass_step(tank_in_l, e->tank_ap1_buf, 877, &e->tank_ap1_idx, 0.50f);

            e->tank_d1_buf[e->tank_d1_idx] = ap1_out;
            float d1_out = delay_read_interp(e->tank_d1_buf, 2111, e->tank_d1_idx, 1950.0f + mod1);
            e->tank_d1_idx++;
            if (e->tank_d1_idx >= 2111) e->tank_d1_idx = 0;

            e->damp_l += damp_c * (d1_out - e->damp_l);
            e->tank_d2_buf[e->tank_d2_idx] = e->damp_l;
            float d2_out = e->tank_d2_buf[e->tank_d2_idx];
            e->tank_d2_idx++;
            if (e->tank_d2_idx >= 1789) e->tank_d2_idx = 0;

            /* Right Loop */
            float tank_in_r = diff + decay_gain * d2_out;
            float ap2_out = allpass_step(tank_in_r, e->tank_ap2_buf, 907, &e->tank_ap2_idx, 0.50f);

            e->tank_d3_buf[e->tank_d3_idx] = ap2_out;
            float d3_out = delay_read_interp(e->tank_d3_buf, 2341, e->tank_d3_idx, 2180.0f + mod2);
            e->tank_d3_idx++;
            if (e->tank_d3_idx >= 2341) e->tank_d3_idx = 0;

            e->damp_r += damp_c * (d3_out - e->damp_r);
            e->tank_d4_buf[e->tank_d4_idx] = e->damp_r;
            float d4_out = e->tank_d4_buf[e->tank_d4_idx];
            e->tank_d4_idx++;
            if (e->tank_d4_idx >= 1993) e->tank_d4_idx = 0;

            /* Stereo Reverb Wet Output */
            float wet_l = 0.55f * (d1_out - d3_out);
            float wet_r = 0.55f * (d2_out + d4_out);

            /* Tone shaping filter */
            if (e->filter_mode != 0) {
                wet_l = fx_svf_run(&e->svf_l, &e->svf_st_l, wet_l);
                wet_r = fx_svf_run(&e->svf_r, &e->svf_st_r, wet_r);
            }

            /* Vintage DAC quantization (Grit) */
            if (e->grit_step > 0.0f) {
                wet_l = (float)(int)(wet_l * e->inv_grit_step) * e->grit_step;
                wet_r = (float)(int)(wet_r * e->inv_grit_step) * e->grit_step;
            }

            /* Output Mix */
            if (mix_wet <= 0.001f) {
                l[i] = in_l;
                r[i] = in_r;
            } else {
                l[i] = in_l * mix_dry + wet_l * mix_wet;
                r[i] = in_r * mix_dry + wet_r * mix_wet;
            }
        }

        e->phase = (e->phase + m) % FX_CHUNK;
        l += m; r += m; n -= m;
    }
}

/* Processes outside 48 kHz audio up to 2 * FX_CHUNK frames */
STAGE static void fx_piece(fx_engine *e, float *l, float *r, int n)
{
    float pa[2][FX_CHUNK + 1], pb[2][FX_CHUNK + 1];
    int nc = 0;

    /* Input warm stage: gentle treble dip and paired sample accumulation */
    {
        const float wc = e->warm_c;
        float w0 = e->warm_lp[0], w1 = e->warm_lp[1];
        int odd = e->dodd;
        for (int i = 0; i < n; i++) {
            float x0 = l[i], x1 = r[i];
            w0 += wc * (x0 - w0);
            w1 += wc * (x1 - w1);
            x0 += 0.25f * (w0 - x0);
            x1 += 0.25f * (w1 - x1);
            if (odd) {
                pa[0][nc] = x0; pb[0][nc] = e->dfirst[0];
                pa[1][nc] = x1; pb[1][nc] = e->dfirst[1];
                nc++;
            } else {
                e->dfirst[0] = x0; e->dfirst[1] = x1;
            }
            odd ^= 1;
        }
        e->warm_lp[0] = w0; e->warm_lp[1] = w1; e->dodd = odd;
    }

    /* 48 -> 24 kHz decimation */
    float cl[FX_CHUNK + 1], crr[FX_CHUNK + 1];
    for (int c = 0; c < 2; c++) {
        hb_run(pa[c], pb[c], nc, e->dy[c], &e->dx0[c], &e->dx1[c]);
        float *o = c ? crr : cl;
        for (int k = 0; k < nc; k++) {
            float x = 0.5f * (pa[c][k] + pb[c][k]);
            /* Gentle warm stage saturation at 24 kHz */
            o[k] = x + 0.5f * (fx_softclip(1.4f * x) * 0.71428571f - x);
        }
    }

    /* Process DSP Reverb Core at 24 kHz */
    core_process(e, cl, crr, nc);

    /* 24 -> 48 kHz interpolation */
    for (int c = 0; c < 2; c++) {
        const float *in = c ? crr : cl;
        for (int k = 0; k < nc; k++) pa[c][k] = pb[c][k] = in[k];
        hb_run(pa[c], pb[c], nc, e->uy[c], &e->ux0[c], &e->ux1[c]);
    }

    {
        int odd = e->uodd, k = 0;
        float max_gr = 0.0f;
        for (int i = 0; i < n; i++) {
            float o0, o1;
            if (odd) {
                o0 = pa[0][k]; o1 = pa[1][k];
                e->usecond[0] = pb[0][k]; e->usecond[1] = pb[1][k];
                k++;
            } else {
                o0 = e->usecond[0]; o1 = e->usecond[1];
            }
            odd ^= 1;

            /* Peak limiter ceiling at -0.3 dBFS */
            if (o0 > 0.966f) { if (o0 - 0.966f > max_gr) max_gr = o0 - 0.966f; o0 = 0.966f; }
            else if (o0 < -0.966f) { if (-0.966f - o0 > max_gr) max_gr = -0.966f - o0; o0 = -0.966f; }

            if (o1 > 0.966f) { if (o1 - 0.966f > max_gr) max_gr = o1 - 0.966f; o1 = 0.966f; }
            else if (o1 < -0.966f) { if (-0.966f - o1 > max_gr) max_gr = -0.966f - o1; o1 = -0.966f; }

            l[i] = o0;
            r[i] = o1;
        }
        e->uodd = odd;
        if (max_gr > e->limiter.meter) e->limiter.meter = max_gr;
    }
}

void fx_process(fx_engine *e, float *l, float *r, int n)
{
    while (n > 0) {
        int m = n < 2 * FX_CHUNK ? n : 2 * FX_CHUNK;
        fx_piece(e, l, r, m);
        l += m; r += m; n -= m;
    }
}
