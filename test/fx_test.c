/* Host test for the SP-1 Reverb engine: renders WAVs into build/test and checks the behaviour of
 *   the engine and of each of its eight models.
 *   fx_test [OUTDIR]                          run the checks and write the WAVs
 *   fx_test OUTDIR IN.wav MODEL F1 F2 F3 F4   also run a 48 kHz WAV through that model and faders */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../fx/fx.h"

#define SR 48000
static fx_engine E;
static int fails;

static void check(int ok, const char *what, double v)
{
    printf("%-58s %10.4f  %s\n", what, v, ok ? "ok" : "FAIL");
    if (!ok) fails++;
}

/* ---- WAV ---- */
static void put32(FILE *f, unsigned v) { fputc(v, f); fputc(v >> 8, f); fputc(v >> 16, f); fputc(v >> 24, f); }
static void put16(FILE *f, unsigned v) { fputc(v, f); fputc(v >> 8, f); }

static void wav_write(const char *dir, const char *name, const float *l, const float *r, int n)
{
    char p[512];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "wb");
    if (!f) { perror(p); exit(1); }
    fwrite("RIFF", 1, 4, f); put32(f, 36 + n * 6); fwrite("WAVEfmt ", 1, 8, f);
    put32(f, 16); put16(f, 1); put16(f, 2); put32(f, SR); put32(f, SR * 6); put16(f, 6); put16(f, 24);
    fwrite("data", 1, 4, f); put32(f, n * 6);
    for (int i = 0; i < n; i++)
        for (int c = 0; c < 2; c++) {
            double x = (c ? r : l)[i];
            x = x > 1 ? 1 : x < -1 ? -1 : x;
            int v = (int)lrint(x * 8388607.0);
            fputc(v, f); fputc(v >> 8, f); fputc(v >> 16, f);
        }
    fclose(f);
}

static int wav_read(const char *path, float **l, float **r)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return -1; }
    unsigned char h[12];
    if (fread(h, 1, 12, f) != 12 || memcmp(h, "RIFF", 4) || memcmp(h + 8, "WAVE", 4)) { fclose(f); return -1; }
    int ch = 0, bits = 0, rate = 0, n = -1;
    unsigned char c[8];
    while (fread(c, 1, 8, f) == 8) {
        unsigned sz = c[4] | c[5] << 8 | c[6] << 16 | (unsigned)c[7] << 24;
        if (!memcmp(c, "fmt ", 4)) {
            unsigned char fm[40];
            if (fread(fm, 1, sz < 40 ? sz : 40, f) < 16) break;
            if (sz > 40) fseek(f, sz - 40, SEEK_CUR);
            ch = fm[2] | fm[3] << 8; rate = fm[4] | fm[5] << 8 | fm[6] << 16; bits = fm[14] | fm[15] << 8;
        } else if (!memcmp(c, "data", 4)) {
            if ((bits != 16 && bits != 24) || ch < 1 || ch > 2) break;
            if (rate != SR) fprintf(stderr, "warning: %d Hz, played as 48 kHz\n", rate);
            int bps = bits / 8;
            n = (int)(sz / (unsigned)(bps * ch));
            *l = malloc(sizeof(float) * n); *r = malloc(sizeof(float) * n);
            for (int i = 0; i < n; i++)
                for (int k = 0; k < ch; k++) {
                    unsigned char s[3] = {0};
                    if (fread(s, 1, bps, f) != (size_t)bps) { n = i; goto done; }
                    int v = bits == 16 ? (int16_t)(s[0] | s[1] << 8) * 256 : ((int32_t)((unsigned)s[0] << 8 | (unsigned)s[1] << 16 | (unsigned)s[2] << 24)) >> 8;
                    float x = (float)v / 8388608.0f;
                    if (k == 0) { (*l)[i] = x; if (ch == 1) (*r)[i] = x; }
                    else (*r)[i] = x;
                }
            break;
        } else fseek(f, sz + (sz & 1), SEEK_CUR);
    }
done:
    fclose(f);
    return n;
}

/* ---- the test signal: a one-drop reggae bar, 75 BPM ---- */
static double nz(unsigned *s) { *s = *s * 1664525u + 1013904223u; return (double)(int)*s / 2147483648.0; }

static void make_loop(float *l, float *r, int n)
{
    const double beat = 0.8, bar = 4 * beat;
    const double bass[8] = { 55.0, 55.0, 65.41, 73.42, 55.0, 49.0, 55.0, 82.41 }; /* A1 C2 D2 G1 E2 */
    unsigned s = 1;
    double bp1 = 0, bp2 = 0, hh = 0;
    double ph[3] = {0}, bph = 0;
    for (int i = 0; i < n; i++) {
        double t = (double)i / SR, tb = fmod(t, bar), tq = fmod(t, beat);
        int q = (int)(tb / beat);
        int e8 = (int)(fmod(t, 2 * bar) / (beat / 2)) % 16;
        double f = bass[e8 / 2], te = fmod(t, beat / 2);
        bph += 2 * M_PI * f / SR;
        double benv = (1 - exp(-te / 0.004)) * exp(-te / 0.35);
        double b = (sin(bph) + 0.3 * sin(2 * bph)) * benv * 0.45;
        double k = 0, rim = 0;
        if (q == 2) {
            double kp = 2 * M_PI * (50 * tq + 60 * 0.03 * (1 - exp(-tq / 0.03)));
            k = sin(kp) * exp(-tq / 0.18) * 0.7;
            rim = nz(&s) * exp(-tq / 0.012) * 0.35 + sin(2 * M_PI * 1700 * tq) * exp(-tq / 0.02) * 0.2;
        }
        double sk = 0;
        if (q == 1 || q == 3) {
            const double fr[3] = { 220.0, 261.63, 329.63 };
            for (int j = 0; j < 3; j++) { ph[j] += fr[j] / SR; sk += 2 * fmod(ph[j], 1.0) - 1; }
            sk *= exp(-tq / 0.06) * 0.18;
        }
        bp1 += 0.25 * (sk - bp1); bp2 += 0.25 * (bp1 - bp2);
        sk = bp1 - bp2 * 0.6;
        double th = fmod(t, beat / 2), w = nz(&s);
        hh = w - hh * 0.0 ; double hat = (w - 0.7 * hh) * exp(-th / 0.03) * 0.08;
        double mono = b + k + rim;
        l[i] = (float)(mono + sk * 1.1 + hat * 0.8);
        r[i] = (float)(mono + sk * 0.9 + hat * 1.2);
    }
    float pk = 0;
    for (int i = 0; i < n; i++) { if (fabsf(l[i]) > pk) pk = fabsf(l[i]); if (fabsf(r[i]) > pk) pk = fabsf(r[i]); }
    for (int i = 0; i < n; i++) { l[i] *= 0.5f / pk; r[i] *= 0.5f / pk; }
}

/* ---- measuring ---- */
typedef struct { double b0, b1, b2, a1, a2, z1, z2; } bq;
static void bq_set(bq *q, int hp, double hz)
{
    double w = 2 * M_PI * hz / SR, c = cos(w), al = sin(w) / (2 * 0.7071), a0 = 1 + al;
    q->b1 = (hp ? -(1 + c) : (1 - c)) / a0; q->b0 = q->b2 = (hp ? (1 + c) / 2 : (1 - c) / 2) / a0;
    q->a1 = -2 * c / a0; q->a2 = (1 - al) / a0; q->z1 = q->z2 = 0;
}
static double bq_run(bq *q, double x)
{
    double y = q->b0 * x + q->z1;
    q->z1 = q->b1 * x - q->a1 * y + q->z2; q->z2 = q->b2 * x - q->a2 * y;
    return y;
}
static double band_db(const float *x, int from, int n, double lo, double hi)
{
    bq a, b, c, d; bq_set(&a, 1, lo); bq_set(&b, 1, lo); bq_set(&c, 0, hi); bq_set(&d, 0, hi);
    double e = 0;
    for (int i = 0; i < n; i++) {
        double y = bq_run(&d, bq_run(&c, bq_run(&b, bq_run(&a, x[i]))));
        if (i >= from) e += y * y;
    }
    return 10 * log10(e / (n - from) + 1e-30);
}
static double rms_db(const float *x, int from, int n)
{
    double e = 0; for (int i = from; i < n; i++) e += (double)x[i] * (double)x[i];
    return 10 * log10(e / (n - from) + 1e-30);
}
static double peak(const float *l, const float *r, int n)
{
    double p = 0;
    for (int i = 0; i < n; i++) { if (fabs(l[i]) > p) p = fabs(l[i]); if (fabs(r[i]) > p) p = fabs(r[i]); }
    return p;
}
static int finite_all(const float *l, const float *r, int n)
{
    for (int i = 0; i < n; i++) if (!isfinite(l[i]) || !isfinite(r[i])) return 0;
    return 1;
}

static double click_ratio(const float *x, int n)
{
    int nw = n / 256;
    double *m = calloc(nw, sizeof *m), worst = 0;
    for (int w = 0; w < nw; w++)
        for (int i = w * 256 + (w ? 0 : 2); i < w * 256 + 256; i++) {
            double d = fabs(x[i] - 2.0 * x[i - 1] + x[i - 2]);
            if (d > m[w]) m[w] = d;
        }
    for (int w = 2; w < nw - 1; w++) {
        double nb = fmax(fmax(m[w - 1], m[w + 1]), 1e-4);
        if (m[w] / nb > worst) worst = m[w] / nb;
    }
    free(m);
    return worst;
}


static void faders(float f1, float f2, float f3, float f4)
{
    fx_set_fader(&E, FX_MIX, f1);
    fx_set_fader(&E, FX_DECAY, f2);
    fx_set_fader(&E, FX_DIFFUSION, f3);
    fx_set_fader(&E, FX_FILTER, f4);
}

/* a fresh engine on a model (already switched in) */
static void start(int model)
{
    fx_init(&E, SR);
    if (model != FX_HALL) {
        static float zl[4096], zr[4096];
        fx_set_model(&E, model);
        memset(zl, 0, sizeof zl); memset(zr, 0, sizeof zr);
        fx_process(&E, zl, zr, 4096);
    }
}

static void run_m(int model, const float *il, const float *ir, float *ol, float *orr, int n,
                  float f1, float f2, float f3, float f4, int block)
{
    start(model);
    faders(f1, f2, f3, f4);
    memcpy(ol, il, sizeof(float) * n); memcpy(orr, ir, sizeof(float) * n);
    unsigned s = 7;
    for (int i = 0; i < n;) {
        int m = block > 0 ? block : 1 + (int)((s = s * 1103515245u + 12345u) >> 16) % 300;
        if (m > n - i) m = n - i;
        fx_process(&E, ol + i, orr + i, m);
        i += m;
    }
}

/* the reverb alone, for an impulse, MIX at the top */
static void impulse(int model, float decay, float diff, float *l, float *r, int n)
{
    start(model);
    faders(1.0f, decay, diff, 0.5f);
    fx_set_param2(&E, FX_PREDELAY, 0.0f);
    memset(l, 0, 4 * n); memset(r, 0, 4 * n);
    l[200] = r[200] = 0.5f;
    for (int i = 0; i < n; i += 256) fx_process(&E, l + i, r + i, n - i < 256 ? n - i : 256);
}

/* RT60 from the impulse response: Schroeder's backward integral, fitted from -5 to -25 dB */
static double rt60(const float *l, const float *r, int n)
{
    double *e = malloc(sizeof(double) * n), s = 0;
    for (int i = n - 1; i >= 0; i--) { s += (double)l[i] * l[i] + (double)r[i] * r[i]; e[i] = s; }
    int a = -1, b = -1;
    for (int i = 0; i < n; i++) {
        double db = 10 * log10(e[i] / e[0] + 1e-30);
        if (a < 0 && db < -5) a = i;
        if (b < 0 && db < -25) { b = i; break; }
    }
    free(e);
    if (a < 0 || b < 0) return 1e9;
    return 3.0 * (double)(b - a) / SR;
}

static double corr(const float *a, const float *b, int from, int n)
{
    double sab = 0, saa = 0, sbb = 0;
    for (int i = from; i < n; i++) { sab += (double)a[i] * b[i]; saa += (double)a[i] * a[i]; sbb += (double)b[i] * b[i]; }
    return sab / sqrt(saa * sbb + 1e-30);
}

/* how spiky a tail is: in each 40 ms block the loudest 1 ms against the block's average (dB),
 * averaged over the blocks. A dense tail sits near 5 dB; a train of discrete echoes far higher. */
static double flutter_db(const float *x, int from, int to)
{
    const int w = SR / 1000, B = 40;
    double acc = 0; int nb = 0;
    for (int b0 = from; b0 + B * w <= to; b0 += B * w) {
        double mx = 0, sum = 0;
        for (int k = 0; k < B; k++) {
            double e = 0;
            for (int i = 0; i < w; i++) { double v = x[b0 + k * w + i]; e += v * v; }
            sum += e; if (e > mx) mx = e;
        }
        if (sum > 0) { acc += 10 * log10(mx / (sum / B)); nb++; }
    }
    return nb ? acc / nb : 0;
}


/* when a narrow band (around hz) peaks within [from, to): frames */
static int band_peak_at(const float *x, int from, int to, double hz)
{
    double w = 2 * M_PI * hz / SR, al = sin(w) / 8.0, a0 = 1 + al;
    double b0 = al / a0, b2 = -al / a0, a1 = -2 * cos(w) / a0, a2 = (1 - al) / a0, z1 = 0, z2 = 0, env = 0, best = 0;
    int at = -1;
    for (int i = 0; i < to; i++) {
        double y = b0 * x[i] + z1;
        z1 = -a1 * y + z2; z2 = b2 * x[i] - a2 * y;
        env += (y * y - env) * 0.01;
        if (i >= from && env > best) { best = env; at = i; }
    }
    return at;
}

/* amplitude of one frequency (a single DFT bin) */
static double tone_db(const float *x, int from, int n, double hz)
{
    double a = 0, b = 0, w = 2 * M_PI * hz / SR;
    for (int i = from; i < n; i++) { a += x[i] * cos(w * i); b += x[i] * sin(w * i); }
    return 20 * log10(2 * sqrt(a * a + b * b) / (n - from) + 1e-30);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : "build/test";
    const int N = 8 * SR, W = SR;  /* 8 s; measure after the first second */
    float *il = malloc(4 * N), *ir = malloc(4 * N), *l = malloc(4 * N), *r = malloc(4 * N);
    float *l2 = malloc(4 * N), *r2 = malloc(4 * N);
    const int NI = 14 * SR;
    float *hl = malloc(4 * NI), *hr = malloc(4 * NI);
    char name[160];
    make_loop(il, ir, N);
    wav_write(dir, "loop_dry.wav", il, ir, N);
    int lat = fx_latency(&E);

    /* math */
    double et = 0, ee = 0, el = 0;
    for (double x = 0.001; x < 1.45; x += 0.001) { double t = tan(x); et = fmax(et, fabs(fx_tanf((float)x) - t) / t); }
    for (double x = -30; x < 30; x += 0.01) ee = fmax(ee, fabs(fx_exp2f((float)x) / exp2(x) - 1));
    for (double x = 1e-6; x < 100; x *= 1.01) el = fmax(el, fabs(fx_log2f((float)x) - log2(x)));
    check(et < 1e-5, "math: tan relative error", et);
    check(ee < 1e-5, "math: exp2 relative error", ee);
    check(el < 1e-4, "math: log2 absolute error", el);

    /* ---- MIX at the bottom: the input, exactly ---- */
    {
        run_m(FX_HALL, il, ir, l, r, N, 0.0f, 0.5f, 0.7f, 0.5f, 256);
        int exact = 1;
        for (int i = lat; i < N; i++) if (l[i] != il[i - lat] || r[i] != ir[i - lat]) { exact = 0; break; }
        check(exact, "dry: bit-exact input at the stated latency", exact);
        float *tl = malloc(4 * N), *tr = malloc(4 * N);
        for (int i = 0; i < N; i++) tl[i] = tr[i] = 0.25f * (float)sin(2 * M_PI * 18000.0 * i / SR);
        run_m(FX_HALL, tl, tr, l2, r2, N, 0.0f, 0.5f, 0.7f, 0.5f, 256);
        double g = rms_db(l2, W, N) - rms_db(tl, W, N);
        check(fabs(g) < 0.01, "dry: an 18 kHz tone passes (full bandwidth) (dB)", g);
        /* and the reverb itself reaches the top too */
        run_m(FX_PLATE, tl, tr, l2, r2, N, 1.0f, 0.5f, 0.7f, 0.5f, 256);
        g = rms_db(l2, W, N) - rms_db(tl, W, N);
        check(g > -30, "wet: an 18 kHz tone makes reverb (PLATE) (dB)", g);
        free(tl); free(tr);
    }

    /* ---- every model ---- */
    for (int m = 0; m < FX_MODELS; m++) {
        const char *mn = fx_model_name(m);
        int gate = m == FX_GATED || m == FX_REVERSE;

        /* the output does not depend on the block size */
        run_m(m, il, ir, l, r, N, 0.6f, 0.6f, 0.7f, 0.3f, 256);
        run_m(m, il, ir, l2, r2, N, 0.6f, 0.6f, 0.7f, 0.3f, 0);
        snprintf(name, sizeof name, "%s: blocks of 256 and of 1..300, same output", mn);
        check(!memcmp(l, l2, 4 * N) && !memcmp(r, r2, 4 * N), name, 1);

        /* the reverb at full, on the loop: there, finite, near the dry level, under the ceiling */
        run_m(m, il, ir, l, r, N, 1.0f, 0.6f, 0.7f, 0.5f, 256);
        snprintf(name, sizeof name, "reverb_%s.wav", mn);
        for (char *c = name; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32;
        wav_write(dir, name, l, r, N);
        snprintf(name, sizeof name, "%s: all samples finite", mn);
        check(finite_all(l, r, N), name, 1);
        double lv = rms_db(l, W, N) - rms_db(il, W, N);
        snprintf(name, sizeof name, "%s: reverb level vs dry (dB)", mn);
        check(lv > -8 && lv < 6, name, lv);
        snprintf(name, sizeof name, "%s: stereo (L/R correlation of the reverb)", mn);
        double cc = corr(l, r, W, N);
        check(cc < 0.8, name, cc);

        /* loud: decay at the top, input +5.6 dB hot: still finite and under the ceiling */
        for (int i = 0; i < N; i++) { l2[i] = il[i] * 1.9f; r2[i] = ir[i] * 1.9f; }
        float *ol = malloc(4 * N), *orr = malloc(4 * N);
        run_m(m, l2, r2, ol, orr, N, 0.7f, 1.0f, 1.0f, 0.5f, 256);
        snprintf(name, sizeof name, "%s: decay full, hot input: peak under -0.3 dBFS", mn);
        check(finite_all(ol, orr, N) && peak(ol, orr, N) <= 0.9661, name, peak(ol, orr, N));
        free(ol); free(orr);

        /* silence in, silence out */
        {
            float *zl = calloc(N, 4), *zr = calloc(N, 4);
            run_m(m, zl, zr, l, r, N, 1.0f, 1.0f, 0.7f, 0.5f, 256);
            snprintf(name, sizeof name, "%s: silence in, silence out", mn);
            check(peak(l, r, N) == 0.0, name, peak(l, r, N));
            free(zl); free(zr);
        }

        /* the decay time follows DECAY */
        if (!gate) {
            impulse(m, 0.25f, 0.7f, hl, hr, NI);
            double t0 = rt60(hl + 200, hr + 200, NI - 200), want0 = fx_decay_seconds(m, 0.25f);
            impulse(m, 0.6f, 0.7f, hl, hr, NI);
            double t1 = rt60(hl + 200, hr + 200, NI - 200), want1 = fx_decay_seconds(m, 0.6f);
            snprintf(name, sizeof name, "%s: RT60 at DECAY 0.25 vs set (ratio)", mn);
            check(t0 / want0 > 0.5 && t0 / want0 < 2.0, name, t0 / want0);
            snprintf(name, sizeof name, "%s: RT60 at DECAY 0.6 vs set (ratio)", mn);
            check(t1 / want1 > 0.5 && t1 / want1 < 2.0, name, t1 / want1);
            snprintf(name, sizeof name, "ir_%s.wav", mn);
            for (char *c = name; *c; c++) if (*c >= 'A' && *c <= 'Z') *c += 32;
            wav_write(dir, name, hl, hr, 4 * SR);
        }

        /* every fader and page-2 control swept on a steady tone: no clicks */
        {
            for (int i = 0; i < N; i++) l2[i] = r2[i] = 0.25f * (float)sin(2 * M_PI * 220.0 * i / SR);
            start(m);
            memcpy(l, l2, 4 * N); memcpy(r, r2, 4 * N);
            for (int i = 0; i < N; i += 64) {
                double t = (double)i / N;
                double tr_ = t < 0.5 ? 2 * t : 2 - 2 * t;
                faders((float)tr_, (float)(0.5 + 0.5 * sin(2 * M_PI * 2 * t)), (float)tr_, (float)(1 - tr_));
                fx_set_param2(&E, FX_MOD_DEPTH, (float)tr_);
                fx_set_param2(&E, FX_MOD_RATE, (float)tr_);
                fx_set_param2(&E, FX_PREDELAY, (float)(0.5 - 0.5 * cos(2 * M_PI * t)));
                fx_set_param2(&E, FX_GRIT, (float)tr_);
                fx_process(&E, l + i, r + i, 64);
            }
            double c = fmax(click_ratio(l, N), click_ratio(r, N));
            snprintf(name, sizeof name, "%s: sweep every control: no clicks", mn);
            check(c < 15 && finite_all(l, r, N), name, c);
        }
    }

    /* ---- model-specific ---- */
    {   /* longer DECAY, longer reverb */
        impulse(FX_HALL, 0.2f, 0.7f, hl, hr, NI);
        double a = rt60(hl + 200, hr + 200, NI - 200);
        impulse(FX_HALL, 0.6f, 0.7f, hl, hr, NI);
        double b = rt60(hl + 200, hr + 200, NI - 200);
        check(b > 2 * a, "HALL: DECAY 0.6 rings longer than 0.2 (s)", b);
        /* ROOM is shorter than HALL at the same DECAY */
        impulse(FX_ROOM, 0.6f, 0.7f, hl, hr, NI);
        double c = rt60(hl + 200, hr + 200, NI - 200);
        check(c < b / 2, "ROOM: shorter than HALL at the same DECAY (s)", c);
    }
    {   /* GATED: dense while the hit lasts, then cut off at the gate time */
        float dec = 0.4f;
        double gt = fx_decay_seconds(FX_GATED, dec);
        int n = 3 * SR;
        start(FX_GATED); faders(1.0f, dec, 0.7f, 0.5f); fx_set_param2(&E, FX_PREDELAY, 0.0f);
        memset(hl, 0, 4 * n); memset(hr, 0, 4 * n);
        unsigned s = 5;
        for (int i = SR / 2; i < SR / 2 + SR / 20; i++) hl[i] = hr[i] = (float)(0.5 * nz(&s));  /* a 50 ms hit */
        fx_process(&E, hl, hr, n);
        int open0 = SR / 2 + SR / 20, closed = open0 + (int)(gt * SR) + SR / 20;
        double during = rms_db(hl, open0, open0 + (int)(gt * SR * 0.8)), after = rms_db(hl, closed, closed + SR / 4);
        check(during > -40, "GATED: dense while open (dBFS)", during);
        check(after - during < -50, "GATED: cut off after the gate time (dB)", after - during);
        wav_write(dir, "gated_hit.wav", hl, hr, n);
    }
    {   /* REVERSE: swells up after a hit, then cuts off */
        float dec = 0.5f;
        double len = fx_decay_seconds(FX_REVERSE, dec);
        int n = 3 * SR, h = SR / 2;
        start(FX_REVERSE); faders(1.0f, dec, 0.7f, 0.5f); fx_set_param2(&E, FX_PREDELAY, 0.0f);
        memset(hl, 0, 4 * n); memset(hr, 0, 4 * n);
        for (int i = h; i < h + 480; i++) hl[i] = hr[i] = 0.5f * (float)sin(2 * M_PI * 300.0 * i / SR);
        fx_process(&E, hl, hr, n);
        int L = (int)(len * SR);
        double first = rms_db(hl, h + L / 10, h + L / 2), second = rms_db(hl, h + L / 2, h + L * 9 / 10);
        double after = rms_db(hl, h + L + SR / 20, h + L + SR / 4);
        check(second - first > 6, "REVERSE: louder at the end of the swell than the start (dB)", second - first);
        check(after - second < -40, "REVERSE: cut off after the swell (dB)", after - second);
        wav_write(dir, "reverse_hit.wav", hl, hr, n);
    }
    {   /* SHIMMER: the octave up builds in the tail; HALL has far less of it */
        double oct[2]; const int ms[2] = { FX_SHIMMER, FX_HALL };
        for (int k = 0; k < 2; k++) {
            int n = 4 * SR;
            start(ms[k]); faders(1.0f, 0.6f, 0.7f, 0.5f); fx_set_param2(&E, FX_MOD_DEPTH, 0.0f);
            memset(hl, 0, 4 * n); memset(hr, 0, 4 * n);
            for (int i = 0; i < SR / 2; i++) hl[i] = hr[i] = 0.3f * (float)sin(2 * M_PI * 440.0 * i / SR);
            fx_process(&E, hl, hr, n);
            oct[k] = tone_db(hl, SR, 3 * SR, 880.0) - tone_db(hl, SR, 3 * SR, 440.0);
            if (!k) wav_write(dir, "shimmer_tone.wav", hl, hr, n);
        }
        check(oct[0] > -10, "SHIMMER: the octave (880 Hz) in the tail vs 440 Hz (dB)", oct[0]);
        check(oct[0] - oct[1] > 15, "SHIMMER: far more octave than HALL (dB)", oct[0] - oct[1]);
    }
    {   /* SPRING: dispersion: in the first trip the lows arrive well after the highs (the drip) */
        impulse(FX_SPRING, 0.0f, 0.7f, hl, hr, SR);
        int a = 200 + 4 * 263, b = a + 4 * 330;   /* the first trip */
        double lo = band_peak_at(hl, a, b, 400), hi = band_peak_at(hl, a, b, 3000);
        check(lo - hi > 480, "SPRING: 400 Hz arrives after 3 kHz in a trip, the drip (frames)", lo - hi);
    }
    {   /* SPRING: a hard hit crashes the springs (a burst of rattle); a soft one only rings them */
        double e[2]; const float amp[2] = { 0.03f, 0.9f };
        for (int k = 0; k < 2; k++) {
            start(FX_SPRING); faders(1.0f, 0.3f, 0.7f, 0.5f); fx_set_param2(&E, FX_PREDELAY, 0.0f);
            memset(hl, 0, 4 * SR); memset(hr, 0, 4 * SR);
            for (int i = 0; i < 96; i++) hl[SR / 10 + i] = hr[SR / 10 + i] = amp[k] * (float)sin(2 * M_PI * 250.0 * i / SR);
            for (int i = 0; i < SR; i += 256) fx_process(&E, hl + i, hr + i, SR - i < 256 ? SR - i : 256);
            e[k] = band_db(hl, SR / 10 + 200, SR / 2, 2000, 4500) - 20 * log10(amp[k]);
            if (k) wav_write(dir, "spring_crash.wav", hl, hr, SR);
        }
        check(e[1] - e[0] > 10, "SPRING: a hard hit crashes: rattle over a soft one, per level (dB)", e[1] - e[0]);
    }
    {   /* SPRING: a smeared tail, not a train of discrete clicks (it was ~16 dB; HALL ~4) */
        for (int k = 0; k < 2; k++) {
            float dif = k ? 0.0f : 0.7f;
            impulse(FX_SPRING, 0.5f, dif, hl, hr, SR);
            double f = fmax(flutter_db(hl, SR / 10, SR * 7 / 10), flutter_db(hr, SR / 10, SR * 7 / 10));
            check(f < 8, k ? "SPRING: smooth tail, DIFFUSION at the bottom (dB)" : "SPRING: smooth tail, no echo train (dB)", f);
            if (!k) wav_write(dir, "spring_impulse.wav", hl, hr, SR);
        }
    }
    {   /* switching models while it plays: no clicks, and the dry never moves */
        for (int i = 0; i < N; i++) l2[i] = r2[i] = 0.25f * (float)sin(2 * M_PI * 220.0 * i / SR);
        start(FX_HALL); faders(0.5f, 0.6f, 0.7f, 0.5f);
        memcpy(l, l2, 4 * N); memcpy(r, r2, 4 * N);
        for (int i = 0, k = 0; i < N; i += 128) {
            if (i % (SR / 2) < 128) fx_set_model(&E, (k++) % FX_MODELS);
            fx_process(&E, l + i, r + i, 128);
        }
        double c = fmax(click_ratio(l, N), click_ratio(r, N));
        check(c < 15, "models: switching every 0.5 s while playing: no clicks", c);
        wav_write(dir, "model_switching.wav", l, r, N);
    }
    {   /* filter: shapes the reverb only */
        double lo0, hi0;
        run_m(FX_PLATE, il, ir, l2, r2, N, 1.0f, 0.5f, 0.7f, 0.5f, 256);
        lo0 = band_db(l2, W, N, 40, 150); hi0 = band_db(l2, W, N, 3000, 9000);
        run_m(FX_PLATE, il, ir, l, r, N, 1.0f, 0.5f, 0.7f, 1.0f, 256);
        check(lo0 - band_db(l, W, N, 40, 150) > 20, "filter up: high-pass cuts the reverb's lows (dB)", lo0 - band_db(l, W, N, 40, 150));
        run_m(FX_PLATE, il, ir, l, r, N, 1.0f, 0.5f, 0.7f, 0.0f, 256);
        check(hi0 - band_db(l, W, N, 3000, 9000) > 20, "filter down: low-pass cuts the reverb's highs (dB)", hi0 - band_db(l, W, N, 3000, 9000));
        run_m(FX_PLATE, il, ir, l, r, N, 1.0f, 0.5f, 0.7f, 0.41f, 256);
        int flat = !memcmp(l, l2, 4 * N) && !memcmp(r, r2, 4 * N);
        run_m(FX_PLATE, il, ir, l, r, N, 1.0f, 0.5f, 0.7f, 0.59f, 256);
        flat = flat && !memcmp(l, l2, 4 * N) && !memcmp(r, r2, 4 * N);
        check(flat, "filter: flat anywhere in the middle fifth (0.41 and 0.59)", flat);
        /* and never the dry */
        run_m(FX_PLATE, il, ir, l, r, N, 0.0f, 0.5f, 0.7f, 0.0f, 256);
        int dry = 1;
        for (int i = lat; i < N; i++) if (l[i] != il[i - lat]) { dry = 0; break; }
        check(dry, "filter: the dry is untouched (MIX at the bottom, filter down)", dry);
    }
    {   /* grit: gritty, but a quiet tail is not cut off */
        impulse(FX_HALL, 0.5f, 0.7f, hl, hr, 4 * SR);
        double t0 = rms_db(hl, 2 * SR, 4 * SR);
        start(FX_HALL); faders(1.0f, 0.5f, 0.7f, 0.5f);
        fx_set_param2(&E, FX_PREDELAY, 0.0f); fx_set_param2(&E, FX_GRIT, 1.0f);
        memset(hl, 0, 16 * SR); memset(hr, 0, 16 * SR); hl[200] = hr[200] = 0.5f;
        fx_process(&E, hl, hr, 4 * SR);
        double t1 = rms_db(hl, 2 * SR, 4 * SR);
        check(fabs(t1 - t0) < 3, "grit at full: the late tail keeps its level (dB)", t1 - t0);
    }
    {   /* pre-delay: the reverb starts that much later */
        double at[2]; const float pv[2] = { 0.0f, 0.5f };
        for (int k = 0; k < 2; k++) {
            start(FX_PLATE); faders(1.0f, 0.4f, 0.7f, 0.5f); fx_set_param2(&E, FX_PREDELAY, pv[k]);
            float *z = calloc(SR, 4), *zr = calloc(SR, 4);
            for (int i = 0; i < SR; i += 256) fx_process(&E, z + i, zr + i, SR - i < 256 ? SR - i : 256);   /* settle */
            memset(z, 0, 4 * SR); memset(zr, 0, 4 * SR); z[0] = zr[0] = 0.5f;
            fx_process(&E, z, zr, SR);
            int first = -1;
            for (int i = 0; i < SR; i++) if (fabsf(z[i]) > 1e-4f) { first = i; break; }
            at[k] = 1000.0 * first / SR;
            free(z); free(zr);
        }
        check(at[1] - at[0] > 72 && at[1] - at[0] < 78, "pre-delay at the middle: the reverb starts 75 ms later (ms)", at[1] - at[0]);
    }

    if (argc > 7) {
        float *wl, *wr;
        int n = wav_read(argv[2], &wl, &wr);
        if (n <= 0) { fprintf(stderr, "can't read %s\n", argv[2]); return 1; }
        start(atoi(argv[3]));
        faders((float)atof(argv[4]), (float)atof(argv[5]), (float)atof(argv[6]), (float)atof(argv[7]));
        for (int i = 0; i < n; i += 256) fx_process(&E, wl + i, wr + i, n - i < 256 ? n - i : 256);
        wav_write(dir, "yours.wav", wl, wr, n);
        printf("wrote %s/yours.wav\n", dir);
    }

    printf("%s: %d failed\n", fails ? "FAIL" : "PASS", fails);
    return fails != 0;
}
