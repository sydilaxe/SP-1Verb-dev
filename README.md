# SP-1Verb-dev: Dedicated Early Digital Reverb for Teenage Engineering SP-1

A dedicated algorithmic early digital reverberator for the **Teenage Engineering SP-1** stem player (Nordic nRF52840 ARM Cortex-M4 @ 64 MHz), featuring live USB audio streaming, 4-slider tactile control, and Play-switched real-time tone character modulation.

Designed for the physical screenless SP-1 hardware based on [timknapen/SP-1-dev](https://github.com/timknapen/SP-1-dev) and flashable via [solderless.engineering](https://solderless.engineering).

---

## 🎛️ Control Mapping

### SET 1: Core Reverb Controls (Default / Bank A)
1. **Slider 1: Reverb Mix** — Constant-power sinusoidal Dry/Wet blend.
2. **Slider 2: Decay** — Feedback loop scaling calibrated to RT60 reverb time (0.2s tight space up to 25.0s infinite wash).
3. **Slider 3: Diffusion** — 3-stage all-pass smearing (from grainy early digital echoes to dense clouds).
4. **Slider 4: HP / LP Filter** — Tank frequency damping cutoff (selectable Low-pass, Band-pass, High-pass).

### SET 2: Real-Time Tone Character Modulation (Engaged by PLAY)
When the central silicone **PLAY** button is pressed, the 4 touch light strips instantly flip to live tone modulation:
1. **Slider 1: Mod Depth (Wander)** — Recirculating delay line chorusing depth.
2. **Slider 2: Mod Rate (Spin)** — LFO spin frequency (0.10 Hz to 6.00 Hz).
3. **Slider 3: Pre-Delay** — 0 to 250 ms early reflection onset gap to keep dry transients punchy.
4. **Slider 4: Vintage Grit** — 12-bit / 8-bit DAC decimation and warm analog saturation.

---

## ⚡ Hardware Constraints & Optimization
- **Target MCU:** Nordic nRF52840 (ARM Cortex-M4 @ 64 MHz with FPU, 256 KB RAM).
- **Zero Screen (Screenless Design):** All parameter levels, states, and algorithm presets are communicated through the 4 embedded touch-sensitive LED light strips.
- **Rocker Switch (`◀` / `▶`):** Steps through 8 historic early digital reverb algorithms (EMT 250, Lexicon 224, AMS Non-Lin, Sony DRE-2000, Ursa Major SST-282, Blade Runner Wash).
- **Low-CPU DSP:** Fixed-delay Schroeder-Dattorro topology with single-precision float math and 24.0 kHz clocking (~14% CPU load).

---

## 🛠️ Building the Binary (`sp1_verb.bin`)

### Option 1: Automated GitHub Actions CI
Every commit and release automatically builds `sp1_verb.bin` and `sp1_verb.hex` via `.github/workflows/build-binary.yml`. You can download the flashable `.bin` directly from the Actions tab or GitHub Releases.

### Option 2: Local Compilation via GCC
```bash
# 1. Install ARM Embedded Toolchain
sudo apt-get update && sudo apt-get install -y gcc-arm-none-eabi binutils-arm-none-eabi

# 2. Build the binary
cd firmware
make

# Flashable output: firmware/build/sp1_verb.bin
```

---

## 🚀 How to Flash via Solderless.engineering

1. Plug your **Teenage Engineering SP-1** into your computer via **USB-C**.
2. Put the SP-1 into bootloader mode by holding down **Track 1 + Track 4** for 3 seconds (the 4 light strips will turn solid).
3. Open **[solderless.engineering](https://solderless.engineering)** in Google Chrome or Edge.
4. Select the compiled `sp1_verb.bin` file and click **Flash Device**.
5. Disconnect and reboot your SP-1. The early digital reverb will run natively with all 4 touch sliders!

---

## 💻 Web Audio Companion & Simulator

To run the interactive web companion (supporting live USB audio ingest, Web Serial link, and screenless SP-1 simulation):

```bash
# Install dependencies
npm install

# Run the development server
npm run dev
```

Open `http://localhost:3000` in Google Chrome or Edge.

---

## 📄 License
Apache-2.0
