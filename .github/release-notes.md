# Teenage Engineering SP-1 Early Digital Reverb Firmware

This release provides the compiled `sp1verb.bin` firmware binary for the Teenage Engineering SP-1 Stem Player (Nordic nRF52840 MCU).

## Features
- **Early Digital Reverb Engine**: Authentic 24 kHz Schroeder-Dattorro stereo feedback delay network with 4 input diffusers, dual modulated cross-coupled tanks, and 1-pole damping.
- **Controls (Bank 1)**:
  - Fader 1: Reverb Dry/Wet Mix
  - Fader 2: Decay Time (0.2s to 20s+ wash)
  - Fader 3: Diffusion (Early reflections scatter & tank density)
  - Fader 4: Filter (HP/LP bi-directional, center flat)
- **Modulation Bank (Bank 2 - toggle via PLAY button)**:
  - Fader 1: Mod Depth (tank chorus/pitch flutter)
  - Fader 2: Mod Rate (LFO speed)
  - Fader 3: Pre-delay (0..150 ms)
  - Fader 4: Vintage Grit (12-bit / 8-bit DAC decimation and warmth)
- **Presets**: 8 historic algorithms (EMT 250, Lexicon 224, AMS Non-Lin, Sony DRE-2000, Ursa Major SST-282, Blade Runner CS-80 Wash, Cathedral, 12-Bit Chamber) selectable via `<` / `>` Rocker switch.
- **Live USB Audio Ingest**: Class-compliant USB Audio (UAC2) 48 kHz / 24-bit stereo stream directly into the reverb tank.

## How to Flash via Solderless
1. Connect SP-1 via USB-C to Chrome / Edge.
2. Hold **TRACK 1 + TRACK 4** while plugging in USB (or hold 3 seconds while powered on) until the 4 track LEDs illuminate solid.
3. Open [solderless.engineering](https://solderless.engineering).
4. Select `sp1verb.bin`, flash, unplug, and reconnect.
