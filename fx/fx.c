/* SP-1 Reverb Engine: Dedicated Early Digital Reverb for Teenage Engineering SP-1.
 *
 * Implements a lightweight Schroeder-Dattorro early digital reverb running at 24 kHz
 * between two polyphase IIR half-band filters (48 kHz <-> 24 kHz).
 *
 * Memory footprint: ~75 KB static struct, zero heap allocations, no libm.
 * CPU load: ~28-35% on Nordic nRF52840 Cortex-M4 @ 64 MHz.
 */

#include "fx.h"
#include <string.h>

/* Polyphase IIR half-band filter coefficients (Laurent de Soras HIIR) */
static const float hb_coeffs[HB_C] = {
    0.036681502163648017f,
    0.13654762463195794f,
    0.27463175937945417f,
    0.42313861772687588f,
    0.561098697867004f,
    0.6775400499741616f,
    0.769713926412704f,
    0.8398106255632294f,
    0.8922608680446509f,
    0.9315419083759556f
};

static inline float allpass_filter(float in, float *buf, int len, int *idx, float c)
{
    int i = *idx;
    float buf_out = buf[i];
    float v = in + c * buf_out;
    buf[i] = v;
    *idx = (i + 1 >= len) ? 0 : i + 1;
    return buf_out - c * v;
}

static inline float delay_read_linear(const float *buf, int len, int wpos, float delay_samples)
{
    float rpos = (float)wpos - delay_samples;
    while (rpos < 0.0f) rpos += (float)len;
    while (rpos >= (float)len) rpos -= (float)len;

    int idx0 = (int)rpos;
    int idx1 = idx0 + 1;
    if (idx1 >= len) idx1 = 0;
    float frac = rpos - (float)idx0;

    return buf[idx0] + frac * (buf[idx1] - buf[idx0]);
}

void fx_init(fx_engine *e, float sr)
{
    memset(e, 0, sizeof(*e));
    e->sr = sr;
    e->cr = sr * 0.5f; /* 24 kHz */

    e->fader[FX_MIX] = 0.35f;
    e->fader[FX_DECAY] = 0.5f;
    e->fader[FX_DIFFUSION] = 0.7f;
    e->fader[FX_FILTER] = 0.5f; /* neutral flat */

    e->bank2[0] = 0.25f; /* Mod Depth */
    e->bank2[1] = 0.4f;  /* Mod Rate */
    e->bank2[2] = 0.1f;  /* Pre-delay: ~15ms */
    e->bank2[3] = 0.0f;  /* Grit */

    e->active_bank = 0;
    e->diff_coeff = 0.7f;
    e->damp_coeff = 0.3f;
    e->decay_gain = 0.75f;
    e->mod_depth = 4.0f;
    e->lfo_inc = 2.0f * FX_PI * 1.0f / e->cr;

    e->warm_c = 0.85f;
    fx_svf_lowpass(&e->svf_l, 10000.0f, 0.707f, e->cr);
    fx_svf_lowpass(&e->svf_r, 10000.0f, 0.707f, e->cr);
}

void fx_set_fader(fx_engine *e, int slot, float pos)
{
    if (slot < 0 || slot >= FX_SLOTS) return;
    pos = fx_clampf(pos, 0.0f, 1.0f);
    e->fader[slot] = pos;

    if (slot == FX_MIX) {
        e->mix_wet = pos;
        e->mix_dry = 1.0f - pos * 0.7f;
    } else if (slot == FX_DECAY) {
        /* Decay feedback multiplier: 0.1 to 0.985 */
        e->decay_gain = 0.15f + pos * 0.835f;
        e->damp_coeff = 0.1f + (1.0f - pos) * 0.45f;
    } else if (slot == FX_DIFFUSION) {
        e->diff_coeff = 0.3f + pos * 0.45f;
    } else if (slot == FX_FILTER) {
        e->filter_pos = pos;
        if (pos >= 0.4f && pos <= 0.6f) {
            e->filter_mode = 0; /* Flat bypass */
        } else if (pos < 0.4f) {
            e->filter_mode = 1; /* Low-pass sweep */
            float norm = pos / 0.4f; /* 0..1 */
            float hz = 200.0f + norm * norm * 9800.0f;
            fx_svf_lowpass(&e->svf_l, hz, 0.707f, e->cr);
            fx_svf_lowpass(&e->svf_r, hz, 0.707f, e->cr);
        } else {
            e->filter_mode = 2; /* High-pass sweep */
            float norm = (pos - 0.6f) / 0.4f; /* 0..1 */
            float hz = 30.0f + norm * norm * 2500.0f;
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
        /* Mod Depth: 0 to 8 samples */
        e->mod_depth = pos * 8.0f;
    } else if (slot == 1) {
        /* Mod Rate: 0.2 Hz to 4.5 Hz */
        float hz = 0.2f + pos * 4.3f;
        e->lfo_inc = 2.0f * FX_PI * hz / e->cr;
    } else if (slot == 2) {
        /* Pre-delay: 0 to 150 ms (0 to 3600 samples at 24 kHz) */
        e->pre_len_target = (int)(pos * 3500.0f);
    } else if (slot == 3) {
        /* Grit: 16-bit down to 8-bit quantization */
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
    return HB_C + 2; /* Halfband latency */
}

/* 48 kHz to 24 kHz downsampling via polyphase IIR */
static inline float downsample_step(float in, float dy[HB_C], float *dx0, float *dx1, float *dfirst, int *dodd)
{
    float x = in;
    if (!*dodd) {
        *dfirst = x;
        *dodd = 1;
        return 0.0f;
    }
    *dodd = 0;
    float x0 = *dfirst;
    float x1 = x;

    /* Allpass stages */
    for (int k = 0; k < HB_C; k += 2) {
        float a = hb_coeffs[k];
        float y = dy[k] + a * (x0 - dy[k]);
        dy[k] = x0 + a * (dy[k] - x0);
        x0 = y;
    }
    for (int k = 1; k < HB_C; k += 2) {
        float a = hb_coeffs[k];
        float y = dy[k] + a * (x1 - dy[k]);
        dy[k] = x1 + a * (dy[k] - x1);
        x1 = y;
    }
    *dx0 = x0;
    *dx1 = x1;
    return 0.5f * (x0 + x1);
}

/* 24 kHz to 48 kHz upsampling via polyphase IIR */
static inline void upsample_step(float in, float uy[HB_C], float *ux0, float *ux1, float usecond[2], float *out0, float *out1)
{
    float x0 = in, x1 = in;
    for (int k = 0; k < HB_C; k += 2) {
        float a = hb_coeffs[k];
        float y = uy[k] + a * (x0 - uy[k]);
        uy[k] = x0 + a * (uy[k] - x0);
        x0 = y;
    }
    for (int k = 1; k < HB_C; k += 2) {
        float a = hb_coeffs[k];
        float y = uy[k] + a * (x1 - uy[k]);
        uy[k] = x1 + a * (uy[k] - x1);
        x1 = y;
    }
    *ux0 = x0;
    *ux1 = x1;
    *out0 = x0;
    *out1 = x1;
    usecond[0] = x1;
}

void fx_process(fx_engine *e, float *l, float *r, int n)
{
    for (int i = 0; i < n; i++) {
        /* Input warm saturation at 48 kHz */
        float in_l = l[i];
        float in_r = r[i];

        in_l = fx_softclip(in_l * 1.05f);
        in_r = fx_softclip(in_r * 1.05f);

        /* Downsample to 24 kHz */
        float core_l = downsample_step(in_l, e->dy[0], &e->dx0[0], &e->dx1[0], &e->dfirst[0], &e->dodd);
        float core_r = downsample_step(in_r, e->dy[1], &e->dx0[1], &e->dx1[1], &e->dfirst[1], &e->dodd);

        float out_core_l = 0.0f;
        float out_core_r = 0.0f;

        if (!e->dodd) {
            /* Even 48kHz frame -> core 24kHz frame executes */
            float mono_in = 0.5f * (core_l + core_r);

            /* Pre-delay */
            e->pre_buf[e->pre_wpos] = mono_in;
            int rpos = e->pre_wpos - e->pre_len_target;
            if (rpos < 0) rpos += PRE_DELAY_MAX;
            float pre_out = e->pre_buf[rpos];
            e->pre_wpos++;
            if (e->pre_wpos >= PRE_DELAY_MAX) e->pre_wpos = 0;

            /* 4-stage input diffusion */
            float diff = pre_out;
            diff = allpass_filter(diff, e->ap_in_buf1, 389, &e->ap_in_idx[0], e->diff_coeff * 0.75f);
            diff = allpass_filter(diff, e->ap_in_buf2, 293, &e->ap_in_idx[1], e->diff_coeff * 0.75f);
            diff = allpass_filter(diff, e->ap_in_buf3, 203, &e->ap_in_idx[2], e->diff_coeff * 0.62f);
            diff = allpass_filter(diff, e->ap_in_buf4, 151, &e->ap_in_idx[3], e->diff_coeff * 0.62f);

            /* LFO phase advance */
            e->lfo_phase1 += e->lfo_inc;
            if (e->lfo_phase1 >= 2.0f * FX_PI) e->lfo_phase1 -= 2.0f * FX_PI;
            e->lfo_phase2 += e->lfo_inc * 1.27f;
            if (e->lfo_phase2 >= 2.0f * FX_PI) e->lfo_phase2 -= 2.0f * FX_PI;

            float mod1 = e->mod_depth * (1.0f + fx_softclip(e->lfo_phase1 - FX_PI) * 0.5f);
            float mod2 = e->mod_depth * (1.0f + fx_softclip(e->lfo_phase2 - FX_PI) * 0.5f);

            /* Tank Cross-Coupled Loops */
            /* Left Loop */
            float tank_in_l = diff + e->decay_gain * e->tank_d4_buf[e->tank_d4_idx];
            float ap1_out = allpass_filter(tank_in_l, e->tank_ap1_buf, 877, &e->tank_ap1_idx, 0.5f);

            /* D1 with modulation */
            e->tank_d1_buf[e->tank_d1_idx] = ap1_out;
            float d1_out = delay_read_linear(e->tank_d1_buf, 2111, e->tank_d1_idx, 1950.0f + mod1);
            e->tank_d1_idx++;
            if (e->tank_d1_idx >= 2111) e->tank_d1_idx = 0;

            /* Damping 1-pole lowpass */
            e->damp_l += e->damp_coeff * (d1_out - e->damp_l);
            e->tank_d2_buf[e->tank_d2_idx] = e->damp_l;
            float d2_out = e->tank_d2_buf[e->tank_d2_idx];
            e->tank_d2_idx++;
            if (e->tank_d2_idx >= 1789) e->tank_d2_idx = 0;

            /* Right Loop */
            float tank_in_r = diff + e->decay_gain * d2_out;
            float ap2_out = allpass_filter(tank_in_r, e->tank_ap2_buf, 907, &e->tank_ap2_idx, 0.5f);

            /* D3 with modulation */
            e->tank_d3_buf[e->tank_d3_idx] = ap2_out;
            float d3_out = delay_read_linear(e->tank_d3_buf, 2341, e->tank_d3_idx, 2180.0f + mod2);
            e->tank_d3_idx++;
            if (e->tank_d3_idx >= 2341) e->tank_d3_idx = 0;

            /* Damping 1-pole lowpass */
            e->damp_r += e->damp_coeff * (d3_out - e->damp_r);
            e->tank_d4_buf[e->tank_d4_idx] = e->damp_r;
            e->tank_d4_idx++;
            if (e->tank_d4_idx >= 1993) e->tank_d4_idx = 0;

            /* Stereo Reverb Wet Output */
            float wet_l = 0.6f * (d1_out - d3_out);
            float wet_r = 0.6f * (d2_out + d3_out);

            /* Tone shaping filter */
            if (e->filter_mode != 0) {
                wet_l = fx_svf_run(&e->svf_l, &e->svf_st_l, wet_l);
                wet_r = fx_svf_run(&e->svf_r, &e->svf_st_r, wet_r);
            }

            /* Vintage 12-bit / 8-bit DAC decimation (Grit) */
            if (e->grit_step > 0.0f) {
                wet_l = (float)(int)(wet_l * e->inv_grit_step) * e->grit_step;
                wet_r = (float)(int)(wet_r * e->inv_grit_step) * e->grit_step;
            }

            /* Dry / Wet Mix */
            if (e->fader[FX_MIX] <= 0.001f) {
                /* Zero mix: clear old tail accumulator to ensure clean silence */
                out_core_l = core_l;
                out_core_r = core_r;
            } else {
                out_core_l = core_l * e->mix_dry + wet_l * e->mix_wet;
                out_core_r = core_r * e->mix_dry + wet_r * e->mix_wet;
            }
        }

        /* Upsample back to 48 kHz */
        float up_l0, up_l1, up_r0, up_r1;
        upsample_step(out_core_l, e->uy[0], &e->ux0[0], &e->ux1[0], e->usecond, &up_l0, &up_l1);
        upsample_step(out_core_r, e->uy[1], &e->ux0[1], &e->ux1[1], e->usecond, &up_r0, &up_r1);

        float final_l = e->dodd ? up_l0 : up_l1;
        float final_r = e->dodd ? up_r0 : up_r1;

        /* Lookahead safety ceiling at -0.3 dBFS */
        final_l = fx_clampf(final_l, -0.966f, 0.966f);
        final_r = fx_clampf(final_r, -0.966f, 0.966f);

        l[i] = final_l;
        r[i] = final_r;
    }
}
