# SP-1 Reverb: Dedicated Early Digital Reverb for Teenage Engineering SP-1

Firmware and Web Audio companion that turns the **Teenage Engineering SP-1** into a dedicated early digital reverb processor. Play audio into it over USB-C (it shows up as a class-compliant sound card, `"SP-1 Reverb"`, on macOS, iOS, Windows, Android, or Linux), sculpt the reverb space with the four touch faders, and hear it on headphones or the built-in speaker.

**Firmware Binary:** [`sp1verb.bin`](sp1verb.bin) (flashable via [solderless.engineering](https://solderless.engineering))

---

## 🎛️ The Controls

### Core Controls (Bank 1 — Default)
| Fader | Parameter | Down | Up |
|---|---|---|---|
| 1 | **Mix** | Dry (0%) | 100% Wet Reverb |
| 2 | **Decay** | 0.2 s tight room | 20.0 s+ / infinite early digital wash |
| 3 | **Diffusion** | Granular scatter echoes | Dense reflection cloud |
| 4 | **Filter** | 2-pole Low-Pass sweeping down | 2-pole High-Pass sweeping up (centre fifth is neutral flat) |

### Modulation Bank (Bank 2 — Engaged by PLAY button)
Pressing the central silicone **PLAY** button toggles Bank 2 for real-time tone character shaping:
| Fader | Parameter | Function |
|---|---|---|
| 1 | **Mod Depth** | Recirculating delay line chorusing & pitch flutter (0..8 samples) |
| 2 | **Mod Rate** | LFO spin rate (0.2 Hz to 4.5 Hz) |
| 3 | **Pre-Delay** | 0 to 150 ms early reflection onset gap to keep dry transients punchy |
| 4 | **Vintage Grit** | 12-bit / 8-bit DAC decimation and non-linear warmth |

### Hardware Buttons & Rocker Switch
| Control | Function |
|---|---|
| **Rocker Switch (`◀` / `▶`)** | Steps backward/forward through 8 historic early digital reverb algorithms |
| **PLAY** | Toggle between Bank 1 (Mix/Decay/Diffusion/Filter) and Bank 2 (Mod Depth/Rate/Pre-delay/Grit) |
| **TRACK 1–4** | Mute or bypass individual fader channels |
| **VOL + / VOL -** | Output volume in 3 dB steps (-60 dB to 0 dB) |
| **FUNCTION (hold 1.5 s)** | Power ON |
| **FUNCTION (hold 2.5 s)** | Power OFF |
| **TRACK 1 + 4 (hold 3 s)** | Restart into the SP-1 bootloader for firmware flashing |

### Screenless LED Light Ladders
- **Track Lights (Faders 1–4):** Reflect the active value of each fader. When a preset is changed via the Rocker switch, the lights flash the algorithm number for 1.2s.
- **Status Lights:** Indicate volume changes and breathe slowly while in Bank 2 (Modulation mode).

---

## 🏛️ Comparison with SP-1FX Architecture

| Feature | SP-1FX Reference | SP-1 Reverb (`SP-1Verb-dev`) |
|---|---|---|
| **Application** | 4 multi-effects (Dub tape, Filter, Bitcrush, Limiter) | Dedicated Schroeder-Dattorro Early Digital Reverb unit |
| **Sample Rate Pipeline** | 48 kHz in/out with 24 kHz core processing | 48 kHz in/out with 24 kHz core processing |
| **Half-Band Decimation** | 10-coeff polyphase IIR (-72 dB @ 13k, <-140 dB @ 14k) | Identical 10-coeff polyphase IIR downsampler and upsampler |
| **RAM Footprint** | ~91 KB of 256 KB | ~87 KB of 256 KB (>140 KB SRAM headroom) |
| **CPU Load** | ~70% peak on 64 MHz Cortex-M4 | ~28–35% on 64 MHz Cortex-M4 (well clear of the 80% ceiling) |
| **Memory Allocations** | Zero heap (`malloc`), all static structs | Zero heap, zero `malloc`, all static structs |
| **Math Dependencies** | Pure float, no `libm`, single-cycle `VSQRT.F32` | Pure float, no `libm`, single-cycle `VSQRT.F32` |
| **Bootloader Target** | 0x20000 offset, <= 0xDF000, initial SP in RAM | Exact SP-1 bootloader compliance (passes automated vector verification) |

---

## ⚡ Building & Testing

```bash
# 1. Run host DSP test suite and render check WAVs
make test

# 2. Estimate Cortex-M4 CPU cycles using unicorn emulator
make bench

# 3. Build flashable Zephyr firmware binary (requires Zephyr SDK 0.17.4)
make firmware
```

### Automated GitHub Actions CI
Every commit and push automatically triggers `.github/workflows/build.yml` which:
1. Provisions Zephyr v4.3.1 and Zephyr SDK 0.17.4.
2. Compiles `sp1verb.bin` via `scripts/build-firmware.sh`.
3. Verifies bootloader address offsets (0x20000) and vector table compliance.
4. Executes the host DSP test suite (`make test`).
5. Checks Cortex-M4 instruction cycle counts (`tools/bench_m4.py`).
6. Attaches `sp1verb.bin` as a downloadable GitHub Release artifact.

---

## 🚀 Flashing via Solderless.engineering

1. Connect the SP-1 with a **USB-C** cable to Google Chrome or Microsoft Edge.
2. Enter firmware-loading mode: **Hold TRACK 1 + TRACK 4 while plugging in USB** (or hold them for 3 seconds while powered on). All 4 track lights will come on solid.
3. Open **[solderless.engineering](https://solderless.engineering)**.
4. Select `sp1verb.bin`, flash, unplug, and reconnect.

---

## 📄 Credits & Sources
- **[timknapen/SP-1-dev](https://github.com/timknapen/SP-1-dev)**: Pinouts, hardware peripherals, and bootloader specifications.
- **[chattock/sp1-tape-looper](https://github.com/chattock/sp1-tape-looper)** (MIT): SP-1 board DTS definition, CS42L42 and TAS2505 audio codec initialization, and USB UAC2 class feedback backport.
- **Laurent de Soras**: Polyphase IIR half-band filter design.
