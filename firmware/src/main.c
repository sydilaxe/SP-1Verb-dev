/**
 * Teenage Engineering SP-1 Stem Player - Main Firmware Entry Point
 * Target: Nordic nRF52840 (ARM Cortex-M4 @ 64MHz)
 * Compatible with timknapen/SP-1-dev pinouts and solderless.engineering flasher.
 */

#include "reverb_dsp.h"
#include <stdint.h>
#include <stdbool.h>

#define AUDIO_BLOCK_SIZE 64 // 64 stereo frames = 2.66ms latency @ 24kHz

static sp1_reverb_params_t g_current_params;
static uint8_t g_active_preset_idx = 0;
static bool g_is_playing = false;

// Hardware Slider Touch Positions (0.0 to 1.0)
static float g_slider_values[4] = { 0.42f, 0.45f, 0.72f, 0.50f };

// Mock audio buffers for double-buffering DMA
static int16_t s_audio_rx_buffer[AUDIO_BLOCK_SIZE * 2];
static int16_t s_audio_tx_buffer[AUDIO_BLOCK_SIZE * 2];

// Update Reverb Parameters from Hardware Sliders & Bank State
void sp1_update_controls(void) {
    if (g_current_params.active_bank == 0) {
        // BANK A: CORE REVERB CONTROLS (1-4)
        g_current_params.mix           = g_slider_values[0];
        g_current_params.decay         = 0.2f + g_slider_values[1] * 24.8f; // 0.2s to 25.0s
        g_current_params.diffusion     = g_slider_values[2];
        g_current_params.filter_cutoff = 200.0f + g_slider_values[3] * 15800.0f;
    } else {
        // BANK B: REAL-TIME TONE MODULATION (Switched on PLAY)
        g_current_params.mod_depth     = g_slider_values[0];
        g_current_params.mod_rate      = 0.1f + g_slider_values[1] * 5.9f;  // 0.1Hz to 6.0Hz
        g_current_params.pre_delay_ms  = g_slider_values[2] * 250.0f;       // 0 to 250ms
        g_current_params.vintage_grit  = g_slider_values[3];               // 12-bit / 8-bit DAC
    }
    sp1_reverb_set_params(&g_current_params);
}

// Hardware Button Event: Center PLAY Button
void sp1_on_play_button_pressed(void) {
    g_is_playing = !g_is_playing;
    // CRITICAL: Play button switches to second set of controls for tone modulation!
    if (g_is_playing) {
        g_current_params.active_bank = 1; // Bank B: Mod
    } else {
        g_current_params.active_bank = 0; // Bank A: Core
    }
    sp1_update_controls();
}

// Hardware Button Event: Rocker Switch (Prev / Next Preset)
void sp1_on_rocker_stepped(int8_t direction) {
    int16_t next = (int16_t)g_active_preset_idx + direction;
    if (next < 0) next = SP1_NUM_PRESETS - 1;
    if (next >= SP1_NUM_PRESETS) next = 0;
    g_active_preset_idx = (uint8_t)next;
    sp1_reverb_load_preset(g_active_preset_idx);
}

// Main Audio DMA Callback (runs every 2.66ms at interrupt priority)
void sp1_audio_dma_callback(void) {
    sp1_reverb_process_block(s_audio_rx_buffer, s_audio_tx_buffer, AUDIO_BLOCK_SIZE);
}

int main(void) {
    // 1. Initialize hardware clocks & peripherals
    sp1_reverb_init();
    g_current_params = g_sp1_presets[0].params;

    // 2. Main processing & touch slider poll loop
    while (1) {
        // Read capacitive touch sliders 1-4
        // Read physical Rocker & Center Play button
        sp1_update_controls();
    }

    return 0;
}
