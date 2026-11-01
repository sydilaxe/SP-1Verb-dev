/**
 * SP-1 Early Digital Reverb DSP Header for Nordic nRF52840 (ARM Cortex-M4F @ 64MHz)
 * Schroeder-Dattorro topology with vintage 12-bit DAC decimation and cross-coupled delay tanks.
 */

#ifndef SP1_REVERB_DSP_H
#define SP1_REVERB_DSP_H

#include <stdint.h>
#include <stdbool.h>

#define SP1_SAMPLE_RATE 24000
#define SP1_NUM_PRESETS 8

typedef struct {
    // Bank A: Core Controls (1-4)
    float mix;             // 0.0 to 1.0 (Dry/Wet)
    float decay;           // 0.2 to 25.0 seconds
    float diffusion;       // 0.0 to 1.0 (All-pass smearing)
    float filter_cutoff;   // 200.0 to 16000.0 Hz
    uint8_t filter_mode;   // 0: Low-pass, 1: Band-pass, 2: High-pass

    // Bank B: Tone Modulation Controls (Switched on PLAY)
    float mod_depth;       // 0.0 to 1.0 (Tank wander chorus)
    float mod_rate;        // 0.1 to 6.0 Hz (LFO spin)
    float pre_delay_ms;    // 0 to 250 ms
    float vintage_grit;    // 0.0 to 1.0 (12-bit / 8-bit DAC decimation)

    // Hardware flags
    bool freeze;
    uint8_t active_bank;   // 0: Bank A (Core), 1: Bank B (Mod)
} sp1_reverb_params_t;

typedef struct {
    const char* name;
    uint16_t year;
    sp1_reverb_params_t params;
} sp1_preset_t;

// DSP Engine API
void sp1_reverb_init(void);
void sp1_reverb_set_params(const sp1_reverb_params_t* params);
void sp1_reverb_load_preset(uint8_t preset_index);
void sp1_reverb_process_block(const int16_t* in_stereo, int16_t* out_stereo, uint32_t num_frames);

extern const sp1_preset_t g_sp1_presets[SP1_NUM_PRESETS];

#endif // SP1_REVERB_DSP_H
