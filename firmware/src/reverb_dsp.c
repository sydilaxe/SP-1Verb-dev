/**
 * SP-1 Early Digital Reverb DSP Core Implementation in C
 * Target: Nordic nRF52840 (ARM Cortex-M4F @ 64MHz, 256KB RAM)
 * 
 * Implements:
 * 1. Pre-delay with circular buffer
 * 2. 12-bit / 8-bit DAC quantizer & soft-saturator
 * 3. 3-stage input diffusion all-pass chain
 * 4. Dual cross-coupled modulated delay tanks with 1-pole damping lowpass filters
 * 5. Sinusoidal LFO for tank wander chorusing
 * 6. Dry/Wet constant power matrix
 */

#include "reverb_dsp.h"
#include <math.h>
#include <string.h>

#define PRE_DELAY_MAX_SAMPLES 6000 // 250ms at 24kHz
#define DIFF1_LEN 113  // ~4.7ms
#define DIFF2_LEN 223  // ~9.3ms
#define DIFF3_LEN 307  // ~12.7ms
#define TANK_L_LEN 3408 // ~142ms
#define TANK_R_LEN 4032 // ~168ms
#define TANK_DIFF_L_LEN 720  // ~30ms
#define TANK_DIFF_R_LEN 936  // ~39ms

// Audio buffers allocated in SRAM
static float s_pre_delay_buf[PRE_DELAY_MAX_SAMPLES];
static uint32_t s_pre_delay_idx = 0;

// All-pass buffers
static float s_diff1_buf[DIFF1_LEN];
static uint32_t s_diff1_idx = 0;
static float s_diff2_buf[DIFF2_LEN];
static uint32_t s_diff2_idx = 0;
static float s_diff3_buf[DIFF3_LEN];
static uint32_t s_diff3_idx = 0;

// Tank Buffers
static float s_tank_l_buf[TANK_L_LEN];
static uint32_t s_tank_l_idx = 0;
static float s_tank_r_buf[TANK_R_LEN];
static uint32_t s_tank_r_idx = 0;

static float s_tank_diff_l_buf[TANK_DIFF_L_LEN];
static uint32_t s_tank_diff_l_idx = 0;
static float s_tank_diff_r_buf[TANK_DIFF_R_LEN];
static uint32_t s_tank_diff_r_idx = 0;

// Filter state
static float s_lp_state_l = 0.0f;
static float s_lp_state_r = 0.0f;
static float s_hp_state_l = 0.0f;
static float s_hp_state_r = 0.0f;

// LFO Phase
static float s_lfo_phase = 0.0f;

static sp1_reverb_params_t s_params;

// 8 Historic Early Digital Presets
const sp1_preset_t g_sp1_presets[SP1_NUM_PRESETS] = {
    { "224 Concert Hall", 1978, { 0.45f, 5.2f, 0.82f, 5800.0f, 0, 0.65f, 0.75f, 32.0f, 0.35f, false, 0 } },
    { "EMT 250 Space Plate", 1976, { 0.50f, 3.4f, 0.70f, 4600.0f, 0, 0.45f, 0.60f, 18.0f, 0.55f, false, 0 } },
    { "RMX Non-Lin Snare", 1981, { 0.60f, 1.4f, 0.90f, 8200.0f, 1, 0.20f, 1.20f, 12.0f, 0.40f, false, 0 } },
    { "DRE-2000 Clean Room", 1980, { 0.35f, 2.2f, 0.85f, 9500.0f, 0, 0.30f, 0.45f, 20.0f, 0.15f, false, 0 } },
    { "Space Station SST-282", 1978, { 0.55f, 7.0f, 0.38f, 3800.0f, 0, 0.80f, 1.60f, 45.0f, 0.75f, false, 0 } },
    { "Blade Runner Wash", 1982, { 0.65f, 16.5f, 0.95f, 3200.0f, 0, 0.88f, 0.50f, 60.0f, 0.45f, false, 0 } },
    { "80s Dark Cathedral", 1983, { 0.50f, 11.2f, 0.78f, 2400.0f, 0, 0.50f, 0.35f, 75.0f, 0.30f, false, 0 } },
    { "12-Bit Vintage Chamber", 1979, { 0.50f, 3.8f, 0.52f, 5200.0f, 1, 0.70f, 1.10f, 25.0f, 0.85f, false, 0 } },
};

void sp1_reverb_init(void) {
    memset(s_pre_delay_buf, 0, sizeof(s_pre_delay_buf));
    memset(s_diff1_buf, 0, sizeof(s_diff1_buf));
    memset(s_diff2_buf, 0, sizeof(s_diff2_buf));
    memset(s_diff3_buf, 0, sizeof(s_diff3_buf));
    memset(s_tank_l_buf, 0, sizeof(s_tank_l_buf));
    memset(s_tank_r_buf, 0, sizeof(s_tank_r_buf));
    memset(s_tank_diff_l_buf, 0, sizeof(s_tank_diff_l_buf));
    memset(s_tank_diff_r_buf, 0, sizeof(s_tank_diff_r_buf));
    s_pre_delay_idx = 0;
    s_diff1_idx = s_diff2_idx = s_diff3_idx = 0;
    s_tank_l_idx = s_tank_r_idx = 0;
    s_tank_diff_l_idx = s_tank_diff_r_idx = 0;
    s_lp_state_l = s_lp_state_r = 0.0f;
    s_hp_state_l = s_hp_state_r = 0.0f;
    s_lfo_phase = 0.0f;

    sp1_reverb_load_preset(0);
}

void sp1_reverb_set_params(const sp1_reverb_params_t* params) {
    if (params) {
        s_params = *params;
    }
}

void sp1_reverb_load_preset(uint8_t preset_index) {
    if (preset_index >= SP1_NUM_PRESETS) preset_index = 0;
    s_params = g_sp1_presets[preset_index].params;
}

// Inline All-pass filter step
static inline float allpass_step(float in, float* buf, uint32_t* idx, uint32_t len, float gain) {
    float buf_out = buf[*idx];
    float new_buf = in + gain * buf_out;
    buf[*idx] = new_buf;
    *idx = (*idx + 1) % len;
    return -gain * new_buf + buf_out;
}

// Vintage 12-bit DAC decimation & soft tanh saturation
static inline float vintage_dac_grit(float in, float grit) {
    if (grit <= 0.01f) return in;
    float bits = 16.0f - grit * 8.0f; // 16-bit down to 8-bit
    float steps = powf(2.0f, bits);
    float driven = in * (1.0f + grit * 0.8f);
    float quantized = roundf(driven * (steps * 0.5f)) / (steps * 0.5f);
    // Fast tanh approximation
    float x = quantized;
    float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

void sp1_reverb_process_block(const int16_t* in_stereo, int16_t* out_stereo, uint32_t num_frames) {
    float dry_gain = cosf(s_params.mix * 1.5707963f);
    float wet_gain = sinf(s_params.mix * 1.5707963f);
    float diff_gain = 0.70f * s_params.diffusion;

    // Feedback gain from RT60 decay time
    float fb = s_params.freeze ? 0.999f : powf(0.001f, 0.155f / fmaxf(0.1f, s_params.decay));
    if (fb > 0.96f) fb = 0.96f;

    // Damping LP filter coefficient
    float fc_norm = s_params.filter_cutoff / (float)SP1_SAMPLE_RATE;
    float alpha = fminf(0.95f, fmaxf(0.05f, 2.0f * 3.14159f * fc_norm));

    // Pre-delay in samples
    uint32_t pre_delay_samples = (uint32_t)((s_params.pre_delay_ms / 1000.0f) * SP1_SAMPLE_RATE);
    if (pre_delay_samples >= PRE_DELAY_MAX_SAMPLES) pre_delay_samples = PRE_DELAY_MAX_SAMPLES - 1;

    float lfo_inc = (2.0f * 3.14159265f * s_params.mod_rate) / (float)SP1_SAMPLE_RATE;
    float mod_depth_samples = s_params.mod_depth * 80.0f; // up to ~3.3ms modulation

    for (uint32_t i = 0; i < num_frames; i++) {
        // Convert input from 16-bit int to float [-1.0, 1.0]
        float in_l = (float)in_stereo[i * 2] / 32768.0f;
        float in_r = (float)in_stereo[i * 2 + 1] / 32768.0f;
        float mono_in = (in_l + in_r) * 0.5f;

        // 1. Pre-delay
        s_pre_delay_buf[s_pre_delay_idx] = mono_in;
        uint32_t read_idx = (s_pre_delay_idx + PRE_DELAY_MAX_SAMPLES - pre_delay_samples) % PRE_DELAY_MAX_SAMPLES;
        float delayed_in = s_pre_delay_buf[read_idx];
        s_pre_delay_idx = (s_pre_delay_idx + 1) % PRE_DELAY_MAX_SAMPLES;

        // 2. Vintage DAC Grit
        float gritted = vintage_dac_grit(delayed_in, s_params.vintage_grit);

        // 3. Input Diffusion Cascade (3 All-pass stages)
        float d1 = allpass_step(gritted, s_diff1_buf, &s_diff1_idx, DIFF1_LEN, diff_gain);
        float d2 = allpass_step(d1, s_diff2_buf, &s_diff2_idx, DIFF2_LEN, diff_gain);
        float diffused = allpass_step(d2, s_diff3_buf, &s_diff3_idx, DIFF3_LEN, diff_gain);

        // 4. LFO Wander
        s_lfo_phase += lfo_inc;
        if (s_lfo_phase > 6.2831853f) s_lfo_phase -= 6.2831853f;
        float lfo_val = sinf(s_lfo_phase);
        float mod_offset = lfo_val * mod_depth_samples;

        // 5. Left Tank
        uint32_t read_tank_l = (uint32_t)((float)s_tank_l_idx + TANK_L_LEN - 1 + mod_offset) % TANK_L_LEN;
        float tank_out_l = s_tank_l_buf[read_tank_l];
        float diff_out_l = allpass_step(tank_out_l, s_tank_diff_l_buf, &s_tank_diff_l_idx, TANK_DIFF_L_LEN, 0.6f * s_params.diffusion);
        
        // One-pole Damping Lowpass Filter
        s_lp_state_l += alpha * (diff_out_l - s_lp_state_l);
        // Anti-rumble Highpass (50Hz)
        s_hp_state_l = 0.985f * (s_hp_state_l + s_lp_state_l - s_lp_state_l);

        // 6. Right Tank
        uint32_t read_tank_r = (uint32_t)((float)s_tank_r_idx + TANK_R_LEN - 1 - mod_offset) % TANK_R_LEN;
        float tank_out_r = s_tank_r_buf[read_tank_r];
        float diff_out_r = allpass_step(tank_out_r, s_tank_diff_r_buf, &s_tank_diff_r_idx, TANK_DIFF_R_LEN, 0.6f * s_params.diffusion);
        
        s_lp_state_r += alpha * (diff_out_r - s_lp_state_r);
        s_hp_state_r = 0.985f * (s_hp_state_r + s_lp_state_r - s_lp_state_r);

        // Cross-coupled feedback into tanks
        s_tank_l_buf[s_tank_l_idx] = diffused + fb * (0.7f * s_lp_state_l + 0.3f * s_lp_state_r);
        s_tank_r_buf[s_tank_r_idx] = diffused + fb * (0.7f * s_lp_state_r + 0.3f * s_lp_state_l);

        s_tank_l_idx = (s_tank_l_idx + 1) % TANK_L_LEN;
        s_tank_r_idx = (s_tank_r_idx + 1) % TANK_R_LEN;

        // 7. Wet / Dry Mix Matrix
        float wet_l = s_lp_state_l;
        float wet_r = s_lp_state_r;
        float final_l = in_l * dry_gain + wet_l * wet_gain;
        float final_r = in_r * dry_gain + wet_r * wet_gain;

        // Hard clip protection [-1.0, 1.0] and convert back to 16-bit
        final_l = fmaxf(-1.0f, fminf(1.0f, final_l));
        final_r = fmaxf(-1.0f, fminf(1.0f, final_r));

        out_stereo[i * 2]     = (int16_t)(final_l * 32767.0f);
        out_stereo[i * 2 + 1] = (int16_t)(final_r * 32767.0f);
    }
}
