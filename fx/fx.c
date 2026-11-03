/* SP-1 Reverb engine. See fx.h for the models and the faders.
 *
 * The tank models (HALL, PLATE, ROOM, CLOUD, SHIMMER, GATED, REVERSE): mono into a pre-delay and
 * four allpass diffusers, then two cross-coupled loops (allpass, modulated delay, damping, delay),
 * Dattorro style, scaled to the model's size; stereo taps from both loops. SHIMMER pitches part of
 * the feedback up an octave; GATED and REVERSE put an envelope keyed by the input on the result.
 * SPRING: a tube-driven spring tank (Fisher Space Expander style), run at 12 kHz since a spring
 * carries nothing above ~5 kHz: a driven input, then two springs, each a feedback delay with a
 * long chain of allpasses in its loop (the dispersion: on every trip the lows arrive ~10-15 ms
 * after the highs, the "drip"), a dark top and a thin low end.
 * Then the reverb's filter and grit, the mix, the safety ceiling. */
#include "fx.h"

#ifdef FX_PROFILE   /* tools/bench_m4.py: keep the stages apart so their cost can be read */
#define STAGE __attribute__((noinline))
#else
#define STAGE
#endif

static float chunk_coef(float seconds, float sr)
{
    return 1.0f - fx_exp2f(-1.44269504f * (float)FX_CHUNK / (seconds * sr));
}

static float step_coef(float seconds, float sr, float every)
{
    return 1.0f - fx_exp2f(-1.44269504f * every / (seconds * sr));
}

static inline float glide(float v, float target, float k)
{
    v += (target - v) * k;
    return fx_absf(target - v) < 1e-5f ? target : v;
}

static inline float fx_roundf(float x) { return (float)(int)(x + (x >= 0.0f ? 0.5f : -0.5f)); }

static inline float allpass(float in, float *buf, int len, int *idx, float c)
{
    int i = *idx;
    float d = buf[i];
    float v = in + c * d;
    buf[i] = v;
    *idx = i + 1 < len ? i + 1 : 0;
    return d - c * v;
}

/* the tank's allpasses: a ring of TK_APM, written at *w and read len back; XF also reads nlen
 * back and blends toward it by g (a switch's crossfade) */
static inline __attribute__((always_inline))
float allpass_t(float in, float *buf, int *w, int len, float c, const int XF, int nlen, float g)
{
    int i = *w;
    float d = buf[(i - len) & (TK_APM - 1)];
    if (XF) d += g * (buf[(i - nlen) & (TK_APM - 1)] - d);
    float v = in + c * d;
    buf[i] = v;
    *w = (i + 1) & (TK_APM - 1);
    return d - c * v;
}

/* written at w, read `delay` frames back (fractional) */
static inline float tap(const float *buf, int len, int w, float delay)
{
    int di = (int)delay;
    float fr = delay - (float)di;
    int a = w - di; if (a < 0) a += len;
    int b = a - 1;  if (b < 0) b += len;
    return buf[a] + fr * (buf[b] - buf[a]);
}

static inline float tapi(const float *buf, int len, int w, int delay)
{
    int a = w - delay; if (a < 0) a += len;
    return buf[a];
}

static inline float tri(float p) { return 4.0f * fx_absf(p - 0.5f) - 1.0f; }   /* p 0..1 -> -1..1 */

/* ------------------------------------------------------------------- models ---- */

enum { M_TANK = 0, M_SPRING = 1, M_SHIMMER = 2, M_GATE = 4, M_REVERSE = 8 };

typedef struct {
    const char *name;
    int flags;
    float size;          /* tank scale: 1 = the full delay lines */
    float rt_lo, rt_hi;  /* DECAY's range, seconds (GATED / REVERSE: the gate or swell time) */
    float rt_fixed;      /* GATED / REVERSE: the tank's own RT60 */
    float damp_hz;       /* the tail's top at the shortest decay; darker as it lengthens */
    float diff_scale;    /* input diffusion, times DIFFUSION's */
    float depth_scale;   /* modulation depth, times MOD DEPTH's */
    float tank_c;        /* the tank allpasses */
    float trim;          /* the reverb's level, so each model sits near the dry level */
} model_t;

static const model_t MODELS[FX_MODELS] = {
    /*                 flags      size  rt_lo  rt_hi fixed  damp    diff  depth tank  trim */
    { "HALL",        M_TANK,     1.00f, 0.6f, 30.0f, 0.0f,  8000.f, 1.00f, 1.0f, 0.50f, 1.00f },
    { "PLATE",       M_TANK,     0.55f, 0.4f, 10.0f, 0.0f, 11000.f, 1.20f, 0.4f, 0.62f, 0.95f },
    { "ROOM",        M_TANK,     0.30f, 0.2f,  3.0f, 0.0f,  7000.f, 1.00f, 0.3f, 0.50f, 1.25f },
    { "SPRING",      M_SPRING,   1.00f, 0.4f,  6.0f, 0.0f,  4000.f, 1.00f, 0.0f, 0.00f, 1.15f },
    { "CLOUD",       M_TANK,     1.00f, 2.0f, 60.0f, 0.0f,  5500.f, 0.45f, 3.0f, 0.30f, 1.00f },
    { "SHIMMER",     M_SHIMMER,  0.90f, 1.0f, 30.0f, 0.0f,  9000.f, 1.00f, 1.0f, 0.50f, 1.00f },
    { "GATED",       M_GATE,     0.50f, 0.06f, 0.6f, 2.5f,  9000.f, 1.20f, 0.3f, 0.62f, 0.65f },
    { "REVERSE",     M_REVERSE,  0.60f, 0.15f, 1.0f, 3.0f,  8000.f, 1.10f, 0.3f, 0.60f, 3.20f },
};

const char *fx_model_name(int model)
{
    return model >= 0 && model < FX_MODELS ? MODELS[model].name : "?";
}

float fx_decay_seconds(int model, float pos)
{
    const model_t *m = &MODELS[model >= 0 && model < FX_MODELS ? model : 0];
    pos = fx_clampf(pos, 0.0f, 1.0f);
    return m->rt_lo * fx_exp2f(fx_log2f(m->rt_hi / m->rt_lo) * pos);
}

#define HALF_LOOP_S 0.20f    /* about one trip round half of the full-size tank */
#define SPR_DOWN 4           /* the springs run at 48 / 4 = 12 kHz: a quarter of the work */
#define SPR_L1 263           /* the springs' delays at 12 kHz: 22 and 25 ms, plus the dispersion's */
#define SPR_L2 299
#define SPR_LOOP 90          /* what the dispersion adds to a trip, on average (12 kHz frames) */
#define SPR_DRIVE 1.6f       /* the tube driver: clean when quiet, squashed when hit hard */
#define SPR_CRASH_T 0.20f    /* only a hard hit well over the recent level crashes the springs */
#define SPR_CRASH_G 1.2f     /* and this much rattle per unit over */

static void model_lengths(fx_engine *e, const model_t *m);

static void model_apply(fx_engine *e, int model)
{
    const model_t *m = &MODELS[model];
    e->model = model;
    /* start the reverb clean */
    __builtin_memset(e->ap1, 0, sizeof e->ap1); __builtin_memset(e->ap2, 0, sizeof e->ap2);
    __builtin_memset(e->ap3, 0, sizeof e->ap3); __builtin_memset(e->ap4, 0, sizeof e->ap4);
    __builtin_memset(e->t_ap1, 0, sizeof e->t_ap1); __builtin_memset(e->t_d1, 0, sizeof e->t_d1);
    __builtin_memset(e->t_d2, 0, sizeof e->t_d2); __builtin_memset(e->t_ap2, 0, sizeof e->t_ap2);
    __builtin_memset(e->t_d3, 0, sizeof e->t_d3); __builtin_memset(e->t_d4, 0, sizeof e->t_d4);
    __builtin_memset(e->sh_buf, 0, sizeof e->sh_buf);
    e->sh_lp = 0.0f;
    __builtin_memset(e->pre, 0, sizeof e->pre);   /* the pre-delay too: the old input would be a step */
    __builtin_memset(e->sp_d, 0, sizeof e->sp_d);
    e->sp_in = (fx_svf_state){0}; __builtin_memset(e->sp_y, 0, sizeof e->sp_y);
    e->sp_acc = 0.0f;
    e->sp_hp[0] = e->sp_hp[1] = e->sp_hp[2] = e->sp_hp[3] = 0.0f;
    e->sp_ph = 0;
    e->sp_fast = e->sp_slow = e->sp_crash = 0.0f;
    e->sp_rng = 0x2545F491u;
    fx_svf_lowpass(&e->sp_f1, 4500.0f, 0.7071f, e->sr);
    {   /* the crash's detector and burst run at the springs' rate */
        const float ssr = e->sr / (float)SPR_DOWN;
        e->sp_k[0] = step_coef(0.0003f, ssr, 1.0f);
        e->sp_k[1] = step_coef(0.015f, ssr, 1.0f);
        e->sp_k[2] = step_coef(0.250f, ssr, 1.0f);
        e->sp_k[3] = step_coef(0.060f, ssr, 1.0f);
    }
    for (int k = 0; k < 4; k++) e->ap_i[k] = 0;
    e->ti_ap1 = e->ti_d1 = e->ti_d2 = e->ti_ap2 = e->ti_d3 = e->ti_d4 = 0;
    e->damp_l = e->damp_r = 0.0f;
    e->k_det = e->k_slow = e->k_env = 0.0f; e->k_t = 1 << 30; e->k_run = 0;
    e->f_st[0] = e->f_st[1] = (fx_svf_state){0};
    e->g_env = 0.0f;
    e->xf_n = 0; e->xf_stage = 0;
    e->sh_g = (m->flags & M_SHIMMER) ? 1.0f : 0.0f; e->sh_prime = 0;
    model_lengths(e, m);
    e->ds1 = e->ds3 = e->depth_scale;
}

/* the tank's lengths and settings for a model */
static void model_lengths(fx_engine *e, const model_t *m)
{
    float s = m->size;
    static const int ap_full[4] = { AP_IN1, AP_IN2, AP_IN3, AP_IN4 };
    for (int k = 0; k < 4; k++) {
        int n = (int)((float)ap_full[k] * (0.4f + 0.6f * s));
        e->n_ap[k] = n < 13 ? 13 : n;
    }
    e->n_tap1 = (int)(1753.0f * s); if (e->n_tap1 < 31) e->n_tap1 = 31;   /* tank allpass lengths */
    e->n_tap2 = (int)(1811.0f * s); if (e->n_tap2 < 37) e->n_tap2 = 37;
    e->n_tap1x = (int)(3900.0f * s);                                      /* modulated delays */
    e->n_tap2x = (int)(4360.0f * s);
    e->n_d2 = (int)(3500.0f * s);
    e->n_d4 = (int)(3900.0f * s);
    e->size = s;
    e->tank_c = m->tank_c;
    e->depth_scale = m->depth_scale;
}

/* The models whose tail can carry across a switch: they share the tank, and only its lengths
 * and settings differ. SPRING, GATED and REVERSE fade instead (a gate would cut the tail anyway,
 * and its envelope on top of the crossfade would cost more than any model on its own). */
#define XF_STAGE 32      /* each length's crossfade: 32 chunks, 21 ms */
#define XF_STAGES 6      /* the tank's six lengths, in turn: the whole switch 128 ms */
static int carries(int from, int to)
{
    const int plain = M_TANK | M_SHIMMER;
    return !(MODELS[from].flags & ~plain) && !(MODELS[to].flags & ~plain);
}

static void model_carry(fx_engine *e, int model)
{
    const model_t *m = &MODELS[model], *o = &MODELS[e->model];
    const int d2 = e->n_d2, d4 = e->n_d4, x1 = e->n_tap1x, x2 = e->n_tap2x, t1 = e->n_tap1, t2 = e->n_tap2;
    e->o_tank_c = e->tank_c; e->o_diff_scale = o->diff_scale;
    e->o_fb = e->fb; e->o_damp_c = e->damp_c; e->o_trim = o->trim;
    if ((m->flags & M_SHIMMER) && !(o->flags & M_SHIMMER)) e->sh_prime = 2 * XF_STAGE;  /* fill its window first */
    e->model = model;
    model_lengths(e, m);
    /* the input diffusers take their new lengths at once (they hold only the last few ms of the
     * input); the tank's six lengths crossfade, one at a time */
    for (int k = 0; k < 4; k++) if (e->ap_i[k] >= e->n_ap[k]) e->ap_i[k] = 0;
    e->nx_tap1 = e->n_tap1; e->nx_tap2 = e->n_tap2;
    e->n_tap1 = t1; e->n_tap2 = t2;
    e->nx_d2 = e->n_d2; e->nx_d4 = e->n_d4; e->nx_tap1x = e->n_tap1x; e->nx_tap2x = e->n_tap2x;
    e->nx_ds = e->depth_scale;
    e->n_d2 = d2; e->n_d4 = d4; e->n_tap1x = x1; e->n_tap2x = x2;
    e->xf_stage = 1;
    e->xf_n = XF_STAGE;
}

void fx_set_model(fx_engine *e, int model)
{
    if (model >= 0 && model < FX_MODELS) e->model_next = model;
}

#define FLT_DEAD 0.10f

/* ----------------------------------------------------------------- controls ---- */

STAGE static void control(fx_engine *e)
{
    const float sr = e->sr, k30 = chunk_coef(0.03f, sr);

    /* a model change: carry the tail into the new model, crossfading the tank's lengths; or
     * fade the reverb out (about 20 ms), swap it, fade back in */
    /* (out of SHIMMER, its octave fades first: the two at once would cost too much) */
    const int carry = e->model_next != e->model && e->swg == 1.0f && carries(e->model, e->model_next);
    if (carry && e->xf_stage == 0 && !((MODELS[e->model].flags & M_SHIMMER) && e->sh_g != 0.0f))
        model_carry(e, e->model_next);
    if (e->xf_stage && e->xf_n == 0) {   /* that read is at its new length: on to the next */
        switch (e->xf_stage) {
        case 1: e->n_tap1 = e->nx_tap1; break;
        case 2: e->n_d4 = e->nx_d4; break;
        case 3: e->n_tap1x = e->nx_tap1x; e->ds1 = e->nx_ds; break;
        case 4: e->n_d2 = e->nx_d2; break;
        case 5: e->n_tap2 = e->nx_tap2; break;
        case 6: e->n_tap2x = e->nx_tap2x; e->ds3 = e->nx_ds; break;
        }
        e->xf_stage = e->xf_stage < XF_STAGES ? e->xf_stage + 1 : 0;
        e->xf_n = e->xf_stage ? XF_STAGE : 0;
    }
    e->xf_on = e->xf_stage != 0;
    if (e->xf_on) {             /* the new length's weight ramps 0 to 1 across the stage's chunks */
        e->xg = (float)(XF_STAGE - e->xf_n) * (1.0f / XF_STAGE);
        e->xs = 1.0f / (float)(XF_STAGE * FX_CHUNK);
        /* and the whole switch's progress, for the loop's gain and coefficients */
        e->xp = ((float)(e->xf_stage - 1) + e->xg) * (1.0f / XF_STAGES);
        e->xf_n--;
    } else if (e->model_next != e->model && !carry) {
        e->swg -= 1.0f / 30.0f;
        if (e->swg <= 0.0f) {
            e->swg = 0.0f;
            if (e->wet_g == 0.0f) model_apply(e, e->model_next);
        }
    } else if (e->swg < 1.0f) {
        e->swg += 1.0f / 30.0f;
        if (e->swg > 1.0f) e->swg = 1.0f;
    }
    const model_t *m = &MODELS[e->model];

    /* SHIMMER's octave comes and goes over 43 ms on a carried switch: in, once the lengths are
     * across and then its window is full; out, before the lengths move */
    if ((m->flags & M_SHIMMER) && e->model_next == e->model) {
        if (e->xf_stage) ;
        else if (e->sh_prime > 0) e->sh_prime--;
        else if (e->sh_g < 1.0f) { e->sh_g += 0.5f / XF_STAGE; if (e->sh_g > 1.0f) e->sh_g = 1.0f; }
    } else {
        e->sh_prime = 0;
        if (e->sh_g > 0.0f) { e->sh_g -= 0.5f / XF_STAGE; if (e->sh_g < 1e-4f) e->sh_g = 0.0f; }
    }

    e->mix = glide(e->mix, e->fader[FX_MIX], k30);
    e->decay = glide(e->decay, e->fader[FX_DECAY], k30);
    e->diffusion = glide(e->diffusion, e->fader[FX_DIFFUSION], k30);
    e->depth = glide(e->depth, e->param2[FX_MOD_DEPTH], k30);
    e->grit = glide(e->grit, e->param2[FX_GRIT], k30);
    /* pre-delay moves slowly: a quick move would bend the pitch */
    e->predelay = glide(e->predelay, e->param2[FX_PREDELAY] * (float)(PRE_MAX - 2), chunk_coef(0.1f, sr));

    /* the mix, constant power; ramped across the chunk, landing exactly on the targets */
    e->dry_t = fx_sqrtf(1.0f - e->mix);
    float trim = m->trim;
    if (e->xf_on) trim = e->o_trim + (e->xp + 1.0f / (XF_STAGES * XF_STAGE)) * (trim - e->o_trim);   /* where this chunk ends */
    e->wet_t = fx_sqrtf(e->mix) * e->swg * trim;
    e->dry_step = (e->dry_t - e->dry_g) * (1.0f / FX_CHUNK);
    e->wet_step = (e->wet_t - e->wet_g) * (1.0f / FX_CHUNK);
    /* the reverb's input follows the switch fade, so a fresh model never starts on a step */
    e->in_t = e->swg;
    e->in_step = (e->in_t - e->in_g) * (1.0f / FX_CHUNK);

    /* decay: the loops' gain for the RT60, and a darker tail as it gets longer */
    float rt = (m->flags & (M_GATE | M_REVERSE)) ? m->rt_fixed : fx_decay_seconds(e->model, e->decay);
    const float ssr = sr / (float)SPR_DOWN;                         /* the springs' rate */
    float loop = (m->flags & M_SPRING) ? (float)(SPR_L1 + SPR_LOOP) / ssr : HALF_LOOP_S * e->size;
    e->fb = fx_exp2f(-9.9657843f * loop / rt);                       /* 10^(-3 T / RT60) */
    if ((m->flags & M_SHIMMER) && e->fb > 0.96f) e->fb = 0.96f;       /* the octave adds energy */
    /* the spring's loop is short: the same darkening per trip would kill its tail */
    float fc = (m->flags & M_SPRING) ? m->damp_hz : m->damp_hz * fx_exp2f(-1.3f * e->decay);
    e->damp_c = 1.0f - fx_exp2f(-1.44269504f * 2.0f * FX_PI * fc / ((m->flags & M_SPRING) ? ssr : sr));
    if (m->flags & M_SPRING) {
        e->sp_hc = 1.0f - fx_exp2f(-1.44269504f * 2.0f * FX_PI * 40.0f / ssr);    /* no rumble builds up */
        e->sp_oc = 1.0f - fx_exp2f(-1.44269504f * 2.0f * FX_PI * 220.0f / ssr);   /* the pickup: thin lows */
        e->sp_a = -(0.60f + 0.2f * e->diffusion);   /* negative: the lows arrive last */
    }
    float ds = m->diff_scale;
    if (e->xf_on) {             /* a carried switch: the loop's gain and coefficients glide across */
        e->fb = e->o_fb + e->xp * (e->fb - e->o_fb);
        e->damp_c = e->o_damp_c + e->xp * (e->damp_c - e->o_damp_c);
        ds = e->o_diff_scale + e->xp * (ds - e->o_diff_scale);
        e->tank_c = e->o_tank_c + e->xp * (m->tank_c - e->o_tank_c);
    } else {
        e->tank_c = m->tank_c;
    }
    e->diff_c = fx_clampf((0.30f + 0.45f * e->diffusion) * ds, 0.0f, 0.85f);
    e->lfo_inc = (0.2f + 4.3f * e->param2[FX_MOD_RATE]) / sr;
    if (m->flags & (M_GATE | M_REVERSE)) {
        e->k_len = (int)(fx_decay_seconds(e->model, e->decay) * sr);
        e->k_att = step_coef(0.0005f, sr, 1.0f);
        e->k_rel = step_coef(0.005f, sr, 1.0f);
        e->k_rel_s = step_coef(0.300f, sr, 1.0f);
        e->k_close = step_coef(0.008f, sr, 1.0f);
    }

    /* the reverb's filter: the middle fifth flat */
    e->f_pos = glide(e->f_pos, e->fader[FX_FILTER], chunk_coef(0.02f, sr));
    float p = e->f_pos, t;
    int mode;
    if (p < 0.5f - FLT_DEAD)      { mode = 1; t = (0.5f - FLT_DEAD - p) / (0.5f - FLT_DEAD); }
    else if (p > 0.5f + FLT_DEAD) { mode = 0; t = (p - 0.5f - FLT_DEAD) / (0.5f - FLT_DEAD); }
    else                          { mode = e->f_mode; t = 0.0f; }
    if (e->f_gate == 0.0f && mode != e->f_mode) {
        e->f_mode = mode;
        e->f_st[0] = e->f_st[1] = (fx_svf_state){0};
    }
    e->f_gate_target = t > 0.0f && mode == e->f_mode ? 1.0f : 0.0f;
    if (e->f_gate != 0.0f || e->f_gate_target != 0.0f) {
        t = fx_clampf(t, 0.0f, 1.0f);
        if (e->f_mode == 0) fx_svf_highpass(&e->f_svf, 20.0f * fx_exp2f(7.64f * t), 0.707f, sr);  /* to 4 kHz */
        else fx_svf_lowpass(&e->f_svf, 20000.0f * fx_exp2f(-6.64f * t), 0.707f, sr);           /* to 200 Hz */
    }

    /* grit: 16 bits down to 6, relative to the reverb's level */
    float levels = fx_exp2f(16.0f - 10.0f * e->grit);
    e->g_q = 2.0f / levels;
    e->g_inv_q = 0.5f * levels;
    e->g_wet = fx_clampf(1.5f * e->grit, 0.0f, 1.0f);
}

/* ------------------------------------------------------------------- reverb ---- */

/* pre-delay and the four input diffusers */
static inline float tank_input(fx_engine *e, float in, float c1, float c2, float pd, int *pw)
{
    e->pre[*pw] = in;
    float x = tap(e->pre, PRE_MAX, *pw, pd);
    *pw = *pw + 1 < PRE_MAX ? *pw + 1 : 0;
    x = allpass(x, e->ap1, e->n_ap[0], &e->ap_i[0], c1);
    x = allpass(x, e->ap2, e->n_ap[1], &e->ap_i[1], c1);
    x = allpass(x, e->ap3, e->n_ap[2], &e->ap_i[2], c2);
    return allpass(x, e->ap4, e->n_ap[3], &e->ap_i[3], c2);
}

/* SHIMMER: an octave up, two read heads sweeping a window, crossfaded. Its state is passed in
 * so the tank's loop keeps it in registers. */
#define SH_HP_C 0.0130f   /* its high-pass: 100 Hz at 48 kHz, 1 - e^(-2 pi 100 / 48000) */
static inline __attribute__((always_inline))
float shimmer(float *buf, float x, int *pw, float *pph, float *plp)
{
    int w = *pw;
    buf[w] = x;
    float ph = *pph + 1.0f / (float)SH_LEN;
    if (ph >= 1.0f) ph -= 1.0f;
    *pph = ph;
    float ph2 = ph + 0.5f; if (ph2 >= 1.0f) ph2 -= 1.0f;
    /* the delay shrinks by a frame each frame: read at twice the speed */
    float d1 = (float)(SH_LEN - 2) * (1.0f - ph) + 1.0f, d2 = (float)(SH_LEN - 2) * (1.0f - ph2) + 1.0f;
    int i1 = (int)d1, i2 = (int)d2;
    float f1 = d1 - (float)i1, f2 = d2 - (float)i2;
    int a1 = (w - i1) & (SH_LEN - 1), a2 = (w - i2) & (SH_LEN - 1);   /* SH_LEN is a power of 2 */
    float y1 = buf[a1] + f1 * (buf[(a1 - 1) & (SH_LEN - 1)] - buf[a1]);
    float y2 = buf[a2] + f2 * (buf[(a2 - 1) & (SH_LEN - 1)] - buf[a2]);
    /* windows ~ sin(pi ph) and its half-turn: their powers add to about 1 */
    float y = y1 * (4.0f * ph * (1.0f - ph)) + y2 * (4.0f * ph2 * (1.0f - ph2));
    *pw = (w + 1) & (SH_LEN - 1);
    /* no lows back round the loop: an octave up of the lows is still low (DC is DC), and the
     * windows sum to more than 1 there, so with DECAY high they built up without end */
    float lp = *plp + SH_HP_C * (y - *plp);
    *plp = lp;
    return y - lp;
}

/* The tank. XF: a carried switch is under way, so every length is read twice, the old model's
 * and the new one's, and blended (g1 the new's weight, a straight line from 0 to 1): the sound
 * already in the tank never jumps. */
static inline __attribute__((always_inline))
void tank_run(fx_engine *e, const float *l, const float *r, float *wl, float *wr, int n, const int XF, const int SH)
{
    const float fb = e->fb, dc = e->damp_c, c1 = e->diff_c, c2 = e->diff_c * 0.83f, tc = e->tank_c;
    const float dep = 16.0f * e->depth, dep1 = dep * e->ds1, dep3 = dep * e->ds3, inc = e->lfo_inc, pd = e->predelay;
    const int n1 = e->n_tap1, n2 = e->n_tap2, nd2 = e->n_d2, nd4 = e->n_d4;
    const float x1 = (float)e->n_tap1x, x2 = (float)e->n_tap2x;
    /* SH: 0 no octave, 1 SHIMMER's, 2 coming or going (sh_g of it) */
    const float shg = SH == 1 ? 1.0f : e->sh_g;
    /* XF 1..6: that length is crossfading to its new value (allpass 1, d4, d1, d2, allpass 2, d3) */
    const int nxd = XF == 1 ? e->nx_tap1 : XF == 2 ? e->nx_d4 : XF == 4 ? e->nx_d2 : e->nx_tap2;
    const float nxx = XF == 3 ? (float)e->nx_tap1x : (float)e->nx_tap2x, nxdep = dep * e->nx_ds;
    float g1 = e->xg;
    const float gs = e->xs;
    int shw = e->sh_w;
    float shph = e->sh_ph, shlp = e->sh_lp;
    float l1 = e->lfo1, l2 = e->lfo2, dl = e->damp_l, dr = e->damp_r;
    float ig = e->in_g;
    const float is = e->in_step;
    int pw = e->pre_w;
    for (int i = 0; i < n; i++) {
        if (XF) g1 += gs;
        ig += is;
        float sg = ig * ig * (3.0f - 2.0f * ig);   /* an S-curve: no corners at either end */
        float x = tank_input(e, 0.5f * sg * (l[i] + r[i]), c1, c2, pd, &pw);

        l1 += inc; if (l1 >= 1.0f) l1 -= 1.0f;
        l2 += inc * 1.27f; if (l2 >= 1.0f) l2 -= 1.0f;
        float m1 = 1.0f + tri(l1), m2 = 1.0f + tri(l2);

        /* left loop: fed by the right loop's end */
        float d4o = tapi(e->t_d4, TK_D4, e->ti_d4, nd4);
        if (XF == 2) d4o += g1 * (tapi(e->t_d4, TK_D4, e->ti_d4, nxd) - d4o);
        float back = d4o;
        if (SH == 1) back = 0.75f * d4o + 0.5f * shimmer(e->sh_buf, d4o, &shw, &shph, &shlp);
        else if (SH) back = d4o + shg * (0.5f * shimmer(e->sh_buf, d4o, &shw, &shph, &shlp) - 0.25f * d4o);
        float a = allpass_t(x + fb * back, e->t_ap1, &e->ti_ap1, n1, tc, XF == 1, nxd, g1);
        e->t_d1[e->ti_d1] = a;
        float d1o = tap(e->t_d1, TK_D1, e->ti_d1, x1 + dep1 * m1);
        if (XF == 3) d1o += g1 * (tap(e->t_d1, TK_D1, e->ti_d1, nxx + nxdep * m1) - d1o);
        e->ti_d1 = e->ti_d1 + 1 < TK_D1 ? e->ti_d1 + 1 : 0;
        dl += dc * (d1o - dl);
        e->t_d2[e->ti_d2] = dl;
        float d2o = tapi(e->t_d2, TK_D2, e->ti_d2, nd2);
        if (XF == 4) d2o += g1 * (tapi(e->t_d2, TK_D2, e->ti_d2, nxd) - d2o);
        e->ti_d2 = e->ti_d2 + 1 < TK_D2 ? e->ti_d2 + 1 : 0;

        /* right loop: fed by the left loop's end */
        float b = allpass_t(x + fb * d2o, e->t_ap2, &e->ti_ap2, n2, tc, XF == 5, nxd, g1);
        e->t_d3[e->ti_d3] = b;
        float d3o = tap(e->t_d3, TK_D3, e->ti_d3, x2 + dep3 * m2);
        if (XF == 6) d3o += g1 * (tap(e->t_d3, TK_D3, e->ti_d3, nxx + nxdep * m2) - d3o);
        e->ti_d3 = e->ti_d3 + 1 < TK_D3 ? e->ti_d3 + 1 : 0;
        dr += dc * (d3o - dr);
        e->t_d4[e->ti_d4] = dr;
        e->ti_d4 = e->ti_d4 + 1 < TK_D4 ? e->ti_d4 + 1 : 0;

        /* stereo taps from both loops */
        wl[i] = 0.6f * (d1o - 0.6f * d3o + 0.4f * d4o);
        wr[i] = 0.6f * (d3o - 0.6f * d1o + 0.4f * d2o);
    }
    e->lfo1 = l1; e->lfo2 = l2; e->damp_l = dl; e->damp_r = dr; e->pre_w = pw; e->in_g = ig;
    if (XF) e->xg = g1;
    if (SH) { e->sh_w = shw; e->sh_ph = shph; e->sh_lp = shlp; }
}

static __attribute__((noinline)) void tank_switching(fx_engine *e, const float *l, const float *r, float *wl, float *wr, int n)
{
    if (!e->xf_stage && (e->sh_g != 0.0f || e->sh_prime)) {
        /* the octave only changes while no length is moving */
        tank_run(e, l, r, wl, wr, n, 0, 2);
    } else {
        switch (e->xf_stage) {
        case 1: tank_run(e, l, r, wl, wr, n, 1, 0); break;
        case 2: tank_run(e, l, r, wl, wr, n, 2, 0); break;
        case 3: tank_run(e, l, r, wl, wr, n, 3, 0); break;
        case 4: tank_run(e, l, r, wl, wr, n, 4, 0); break;
        case 5: tank_run(e, l, r, wl, wr, n, 5, 0); break;
        default: tank_run(e, l, r, wl, wr, n, 6, 0); break;
        }
    }
}

STAGE static void reverb_tank(fx_engine *e, const float *l, const float *r, float *wl, float *wr, int n)
{
    /* a switch under way, or the octave coming or going */
    if (e->xf_stage || e->sh_prime || (e->sh_g != 0.0f && e->sh_g != 1.0f)) tank_switching(e, l, r, wl, wr, n);
    else if (e->sh_g != 0.0f) tank_run(e, l, r, wl, wr, n, 0, 1);
    else tank_run(e, l, r, wl, wr, n, 0, 0);
}

/* SPRING: two springs at 12 kHz, each a feedback delay with the dispersion in its loop. The
 * 48 kHz part is kept small (the input, its filter, the output's interpolation): the springs'
 * work happens once every 4 frames. */
STAGE static void reverb_spring(fx_engine *e, const float *l, const float *r, float *wl, float *wr, int n)
{
    const float fb = e->fb, dc = e->damp_c, hc = e->sp_hc, oc = e->sp_oc, pd = e->predelay, a = e->sp_a;
    const float depth = 1.5f * e->depth, inc = e->lfo_inc * (float)SPR_DOWN;
    const fx_svf F = e->sp_f1;
    /* the crash's detector (attack 0.3 ms, release 15 ms, against a 250 ms average) and its burst (60 ms) */
    const float k_att = e->sp_k[0], k_rel = e->sp_k[1], k_avg = e->sp_k[2], k_cr = e->sp_k[3];
    float fast = e->sp_fast, slow = e->sp_slow, cr = e->sp_crash;
    uint32_t rng = e->sp_rng;
    float l1 = e->lfo1, dl = e->damp_l, dr = e->damp_r, h1 = e->sp_hp[0], h2 = e->sp_hp[1];
    float p1 = e->sp_hp[2], p2 = e->sp_hp[3];
    float q1 = e->sp_y[0], c1 = e->sp_y[1], q2 = e->sp_y[2], c2 = e->sp_y[3];
    float ig = e->in_g, acc = e->sp_acc;
    const float is = e->in_step;
    fx_svf_state fs = e->sp_in;
    int pw = e->pre_w, ph = e->sp_ph;
    for (int i = 0; i < n; i++) {
        ig += is;
        float sg = ig * ig * (3.0f - 2.0f * ig);   /* an S-curve: no corners at either end */
        e->pre[pw] = 0.5f * sg * (l[i] + r[i]);
        float x = tap(e->pre, PRE_MAX, pw, pd);
        pw = pw + 1 < PRE_MAX ? pw + 1 : 0;
        /* down to 12 kHz: a low-pass, then the average of each 4 */
        acc += fx_svf_run(&F, &fs, x);

        if (ph == SPR_DOWN - 1) {
            x = acc * (1.0f / SPR_DOWN);
            acc = 0.0f;
            /* the crash: a sharp, hard hit well over the recent level shakes the springs against
             * each other, a burst of rattle into the tank that the springs chirp and ring out */
            float ax = fx_absf(x);
            fast += (ax - fast) * (ax > fast ? k_att : k_rel);
            slow += (fast - slow) * k_avg;
            float over = fast - 3.0f * slow - SPR_CRASH_T;
            if (over > 0.0f && SPR_CRASH_G * over > cr) cr = SPR_CRASH_G * over;
            else cr -= cr * k_cr;
            if (cr > 1e-6f) x += cr * fx_noise(&rng);
            else cr = 0.0f;
            /* the tube driver */
            x = fx_softclip(SPR_DRIVE * x) * (1.0f / SPR_DRIVE);

            l1 += inc; if (l1 >= 1.0f) l1 -= 1.0f;
            float m = depth * (1.0f + tri(l1));
            float s1 = tap(e->t_d1, TK_D1, e->ti_d1, (float)SPR_L1 + m);
            float s2 = tap(e->t_d3, TK_D3, e->ti_d3, (float)SPR_L2 - m);
            /* the dispersion: first-order allpasses (a + z^-1) / (1 + a z^-1), one state each */
            float *b1 = e->sp_d[0], *b2 = e->sp_d[1];
            for (int k = 0; k < SPR_N; k++) {
                float d1 = b1[k], d2 = b2[k];
                float v1 = s1 - a * d1, v2 = s2 - a * d2;
                b1[k] = v1; b2[k] = v2;
                s1 = d1 + a * v1; s2 = d2 + a * v2;
            }
            /* a dark top, a little more each trip, and no rumble */
            dl += dc * (s1 - dl);
            dr += dc * (s2 - dr);
            h1 += hc * (dl - h1);
            h2 += hc * (dr - h2);
            float o1 = dl - h1, o2 = dr - h2;
            /* two springs of different lengths: independent, so the stereo stays wide */
            e->t_d1[e->ti_d1] = x + fb * o1;
            e->t_d3[e->ti_d3] = x - fb * o2;
            e->ti_d1 = e->ti_d1 + 1 < TK_D1 ? e->ti_d1 + 1 : 0;
            e->ti_d3 = e->ti_d3 + 1 < TK_D3 ? e->ti_d3 + 1 : 0;
            /* the pickup hears little below ~200 Hz */
            p1 += oc * (o1 - p1);
            p2 += oc * (o2 - p2);
            q1 = c1; q2 = c2;
            c1 = o1 - p1; c2 = o2 - p2;
            ph = 0;
        } else {
            ph++;
        }
        /* back up to 48 kHz: a straight line between the springs' frames (the top is dark already) */
        float t = (float)ph * (1.0f / SPR_DOWN);
        wl[i] = 2.1f * (q1 + t * (c1 - q1));
        wr[i] = 2.1f * (q2 + t * (c2 - q2));
    }
    e->lfo1 = l1; e->damp_l = dl; e->damp_r = dr;
    e->sp_hp[0] = h1; e->sp_hp[1] = h2; e->sp_hp[2] = p1; e->sp_hp[3] = p2;
    e->pre_w = pw; e->in_g = ig; e->sp_ph = ph; e->sp_acc = acc; e->sp_in = fs;
    e->sp_fast = fast; e->sp_slow = slow; e->sp_crash = cr; e->sp_rng = rng;
    e->sp_y[0] = q1; e->sp_y[1] = c1; e->sp_y[2] = q2; e->sp_y[3] = c2;
}

/* GATED / REVERSE: the input's level opens a gate on the reverb */
STAGE static void envelope(fx_engine *e, const float *l, const float *r, float *wl, float *wr, int n)
{
    const int rev = MODELS[e->model].flags & M_REVERSE, len = e->k_len;
    const float att = e->k_att, rel = e->k_rel, rs = e->k_rel_s, cl = e->k_close;
    const float inv_len = 1.0f / (float)(len > 0 ? len : 1);
    float det = e->k_det, slow = e->k_slow, env = e->k_env;
    int t = e->k_t, run = e->k_run;
    for (int i = 0; i < n; i++) {
        float al = fx_absf(l[i]), ar = fx_absf(r[i]);
        float pk = al > ar ? al : ar;
        det += (pk - det) * (pk > det ? att : rel);
        slow += (det - slow) * rs;
        /* a hit: the level jumps well over its recent average */
        int hit = det > 0.003f && det > 2.0f * slow + 0.002f;
        float target;
        if (rev) {
            if (hit && !run) { run = 1; t = 0; }
            if (run && t >= len) run = 0;
            float u = (float)t * inv_len;
            target = run ? u * u : 0.0f;
            if (run) { env = target; } else env += (0.0f - env) * cl;
        } else {
            if (det > 0.01f) t = 0;          /* held open while the input is there, then the gate time */
            target = t < len ? 1.0f : 0.0f;
            env += (target - env) * (target > env ? att : cl);
        }
        if (t < (1 << 30)) t++;
        wl[i] *= env;
        wr[i] *= env;
    }
    e->k_det = det; e->k_slow = slow; e->k_env = env; e->k_t = t; e->k_run = run;
}

/* the reverb's filter and grit, then the mix into l, r */
STAGE static void color_mix(fx_engine *e, float *l, float *r, float *wl, float *wr, int n)
{
    const float sr = e->sr;
    if (e->f_gate != 0.0f || e->f_gate_target != 0.0f) {
        const fx_svf S = e->f_svf;
        float g = e->f_gate, gt = e->f_gate_target, gstep = 1.0f / (0.015f * sr);
        float a1 = e->f_st[0].ic1, a2 = e->f_st[0].ic2, b1 = e->f_st[1].ic1, b2 = e->f_st[1].ic2;
        for (int i = 0; i < n; i++) {
            if (g != gt) { g += g < gt ? gstep : -gstep; g = fx_clampf(g, 0.0f, 1.0f); }
            float yl = fx_svf_tick(S.a1, S.a2, S.a3, S.m0, S.m1, S.m2, &a1, &a2, wl[i]);
            float yr = fx_svf_tick(S.a1, S.a2, S.a3, S.m0, S.m1, S.m2, &b1, &b2, wr[i]);
            wl[i] += g * (yl - wl[i]);
            wr[i] += g * (yr - wr[i]);
        }
        e->f_gate = g;
        e->f_st[0].ic1 = a1; e->f_st[0].ic2 = a2; e->f_st[1].ic1 = b1; e->f_st[1].ic2 = b2;
    }
    if (e->g_wet > 0.0f) {
        const float q = e->g_q, iq = e->g_inv_q, rel = e->g_rel, w = e->g_wet;
        float env = e->g_env;
        for (int i = 0; i < n; i++) {
            float al = fx_absf(wl[i]), ar = fx_absf(wr[i]);
            float pk = al > ar ? al : ar;
            env = pk > env ? pk : env + rel * (pk - env);
            float lev = env > 1e-5f ? env : 1e-5f;
            float k = iq / lev, s = q * lev;
            float ql = fx_softclip(fx_roundf(wl[i] * k) * s), qr = fx_softclip(fx_roundf(wr[i] * k) * s);
            wl[i] += w * (ql - wl[i]);
            wr[i] += w * (qr - wr[i]);
        }
        e->g_env = env;
    } else {
        e->g_env = 0.0f;
    }

    float dg = e->dry_g, wg = e->wet_g;
    const float ds = e->dry_step, ws = e->wet_step;
    for (int i = 0; i < n; i++) {
        dg += ds; wg += ws;
        l[i] = l[i] * dg + wl[i] * wg;
        r[i] = r[i] * dg + wr[i] * wg;
    }
    e->dry_g = dg; e->wet_g = wg;
}

/* ----------------------------------------------------------- safety ceiling ---- */

/* As in SP-1FX: the input waits LIM_LA frames (6 segments of 8). Once a segment, the gain the
 * outgoing segment ends on is set from the peaks still to come, the full reduction for the next
 * segments ramping down to a fifth for the newest; straight lines in between. Every frame lands
 * at or under the ceiling, the gain has no corners, and at unity the input passes exactly. */
STAGE static void ceiling(fx_engine *e, float *l, float *r, int n)
{
    const float ceil = e->ceiling;
    float acc = e->acc_pk, g = e->sg, gs = e->sg_step, meter = e->meter;
    int p = e->pos_dl, sn = e->sn;
    for (int i = 0; i < n; i++) {
        float xl = l[i], xr = r[i];
        float al = fx_absf(xl), ar = fx_absf(xr);
        if (al > acc) acc = al;
        if (ar > acc) acc = ar;
        float ol = e->dl[0][p], orr = e->dl[1][p];
        e->dl[0][p] = xl; e->dl[1][p] = xr;
        p = p + 1 < LIM_LA ? p + 1 : 0;
        if (g != 1.0f) { ol *= g; orr *= g; }
        l[i] = fx_clampf(ol, -ceil, ceil);
        r[i] = fx_clampf(orr, -ceil, ceil);
        g += gs;
        if (++sn == LIM_SEG) {
            sn = 0;
            int si = e->si;
            e->seg[si] = acc > ceil ? 1.0f - ceil / acc : 0.0f;
            acc = 0.0f;
            float red = 0.0f;
            for (int a = 0; a < LIM_NSEG; a++) {
                int k = si - a; if (k < 0) k += LIM_NSEG;
                float w = a >= LIM_NSEG - 2 ? 1.0f : (float)(a + 1) / (float)(LIM_NSEG - 1);
                float v = e->seg[k] * w;
                if (v > red) red = v;
            }
            e->si = si + 1 < LIM_NSEG ? si + 1 : 0;
            float prev = e->sgr;
            if (red < prev) { red = prev + e->s_rel * (red - prev); if (red < 1e-6f) red = 0.0f; }
            e->sgr = red;
            if (red > meter) meter = red;
            float ge = 1.0f - red;
            g = 1.0f - prev;
            gs = (ge - g) * (1.0f / (float)LIM_SEG);
            if (ge == 1.0f && g == 1.0f) gs = 0.0f;
        }
    }
    e->acc_pk = acc; e->sg = g; e->sg_step = gs; e->meter = meter;
    e->pos_dl = p; e->sn = sn;
}

/* ------------------------------------------------------------------- engine ---- */

void fx_init(fx_engine *e, float sr)
{
    __builtin_memset(e, 0, sizeof *e);
    e->sr = sr;
    e->fader[FX_MIX] = 0.0f;
    e->fader[FX_DECAY] = 0.5f;
    e->fader[FX_DIFFUSION] = 0.7f;
    e->fader[FX_FILTER] = 0.5f;
    e->param2[FX_MOD_DEPTH] = 0.25f;
    e->param2[FX_MOD_RATE] = 0.4f;
    e->param2[FX_PREDELAY] = 0.1f;
    e->param2[FX_GRIT] = 0.0f;
    /* start at rest, not gliding there */
    e->decay = 0.5f; e->diffusion = 0.7f; e->depth = 0.25f;
    e->predelay = 0.1f * (float)(PRE_MAX - 2);
    e->f_pos = 0.5f; e->f_mode = 1;
    e->dry_g = e->dry_t = 1.0f;
    e->lfo2 = 0.37f;
    e->swg = 1.0f;
    e->in_g = e->in_t = 1.0f;
    e->g_rel = step_coef(0.012f, sr, 1.0f);
    e->ceiling = fx_db2lin(-1.0f);
    e->s_rel = step_coef(0.050f, sr, LIM_SEG);
    e->sg = 1.0f;
    model_apply(e, FX_HALL);
    e->model_next = FX_HALL;
}

void fx_set_fader(fx_engine *e, int slot, float pos)
{
    if (slot >= 0 && slot < FX_SLOTS) e->fader[slot] = fx_clampf(pos, 0.0f, 1.0f);
}

void fx_set_param2(fx_engine *e, int slot, float pos)
{
    if (slot >= 0 && slot < FX_SLOTS) e->param2[slot] = fx_clampf(pos, 0.0f, 1.0f);
}

int fx_latency(const fx_engine *e)
{
    (void)e;
    return LIM_LA;
}

float fx_gain_reduction_db(const fx_engine *e)
{
    return -fx_lin2db(1.0f - e->meter + 1e-6f);
}

void fx_process(fx_engine *e, float *l, float *r, int n)
{
    float wl[FX_CHUNK], wr[FX_CHUNK];
    while (n > 0) {
        if (e->phase == 0) control(e);
        int m = FX_CHUNK - e->phase;
        if (m > n) m = n;
        int flags = MODELS[e->model].flags;
        if (flags & M_SPRING) reverb_spring(e, l, r, wl, wr, m);
        else reverb_tank(e, l, r, wl, wr, m);
        if (flags & (M_GATE | M_REVERSE)) envelope(e, l, r, wl, wr, m);
        color_mix(e, l, r, wl, wr, m);
        e->phase = (e->phase + m) % FX_CHUNK;
        if (e->phase == 0) { e->dry_g = e->dry_t; e->wet_g = e->wet_t; e->in_g = e->in_t; }   /* exactly on target */
        ceiling(e, l, r, m);
        l += m; r += m; n -= m;
    }
}
