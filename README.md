# SP-1 Reverb

Firmware that turns the Teenage Engineering SP-1 into a reverb with eight models. Play audio into
it over USB-C (it shows up as a sound card, "SP-1 Reverb", on a computer, phone or tablet), shape
the space with the four faders, and hear it on headphones or the speaker.

**Firmware:** [`sp1verb.bin`](sp1verb.bin) (flash with [solderless.engineering](https://solderless.engineering))

> **Not yet run on a real SP-1.** It builds, the engine passes its tests, and the platform code
> is the same as SP-1FX's, which follows firmware that does run on the SP-1 (see Credits), but
> this exact binary has not been on hardware.

## The models

Pick a model with the TRACK buttons: TRACK n picks model n, and pressing the same button again
flips to model n + 4 (and back). Switching between Hall, Plate, Room, Cloud and Shimmer keeps the
tail: what is already ringing carries on into the new model, which takes over in about 130 ms
(Shimmer's octave fades out first, or in after). Switching to or from Spring, Gated or Reverse
fades the reverb out and back in over about 40 ms instead. The dry signal never moves.

| Button | Model | What it is | DECAY sets |
|---|---|---|---|
| TRACK 1 | **Hall** | a large modulated tank: lush and long | 0.6 to 30 s |
| TRACK 2 | **Plate** | dense and bright, builds fast | 0.4 to 10 s |
| TRACK 3 | **Room** | small and short | 0.2 to 3 s |
| TRACK 4 | **Spring** | a 60s tube spring tank in the style of the Fisher Space Expander: a long "drip" on every trip, thin lows, dark highs, and a crash when a hard hit shakes the springs. DIFFUSION sets how much drip | 0.4 to 6 s |
| TRACK 1, again | **Cloud** | huge, sparse and heavily modulated: a scattered ambient wash | 2 to 60 s |
| TRACK 2, again | **Shimmer** | an octave up in the tank's feedback, so each echo climbs | 1 to 30 s |
| TRACK 3, again | **Gated** | the 80s drum reverb: dense while the hit lasts, then cut off | gate time, 60 to 600 ms |
| TRACK 4, again | **Reverse** | swells up after each hit, then cuts off | swell length, 0.15 to 1 s |

Each model is tested on its own (`make test`): decay time against the setting, stereo width,
level against the dry, no clicks while every control sweeps, silence in giving silence out, and
the same output for any block size. On top of that: the gate closes, the reverse swell rises, the
shimmer carries the octave, the springs drip (in each trip the lows arrive ~12 ms after the highs), crash on a hard hit but not a soft one, and their tails are smooth, not a train of echoes, Shimmer
dies away even with DECAY at the top, switching models while playing doesn't click, and a switch
between the tank models keeps the tail without a click.

## The faders

Two pages of four. **FWD (>>)** goes to page 2, **RWD (<<)** back to page 1. After a page change
a parameter keeps its value until its fader reaches it (or sweeps past it), so nothing jumps; its
track light blinks until then.

| Fader | Page 1 | Page 2 |
|---|---|---|
| 1 | **Mix**: dry at the bottom, all reverb at the top (constant power) | **Mod depth**: chorusing of the tank |
| 2 | **Decay**: the model's reverb time (see above); the tail darkens as it lengthens | **Mod rate**: 0.2 to 4.5 Hz |
| 3 | **Diffusion**: scattered echoes at the bottom, a dense cloud at the top | **Pre-delay**: 0 to 150 ms before the reverb starts |
| 4 | **Filter**: the reverb's tone. The middle fifth is flat; down sweeps a low-pass down to 200 Hz, up a high-pass up to 4 kHz. The dry is never filtered | **Grit**: an early-digital converter, down to 6 bits on the reverb. The bit steps follow the reverb's level, so quiet tails get gritty too instead of cutting off |

After the mix, a **safety ceiling** at -1 dBFS (1 ms look-ahead) is always on and only acts on
overs.

**Full bandwidth, 24-bit:** USB audio is 48 kHz / 24-bit in and out, and the reverb runs at
48 kHz in 32-bit floating point. With Mix at the bottom the output is the input, bit for bit,
48 samples (1 ms) later.

## Other controls

| | |
|---|---|
| PLAY | bypass: the reverb glides out and back (the safety ceiling stays) |
| VOL + / VOL - | output volume, 3 dB steps |
| FUNCTION, hold 1.5 s | power on |
| FUNCTION, hold 2.5 s | power off |
| TRACK 1 + TRACK 4, hold 3 s | restart into the bootloader, for flashing |

Lights: the track lights show the faders of the page you're on (Filter: its distance from the
centre). For 1.2 s after a model change they show the model: its button's light, steady for
models 1-4 and blinking for 5-8. For 0.6 s after FWD or RWD the status lights show the page
(left pair = page 1, right pair = page 2). Otherwise they show the safety ceiling's gain
reduction (the fourth blinking while on page 2), the volume for a second after you change it,
and breathe slowly in bypass. Plug in headphones and the speaker turns off.

## Flashing and getting back

1. Connect the SP-1 with a USB-C cable to Chrome or Edge.
2. Put it in firmware-loading mode: **hold TRACK 1 + TRACK 4 while you plug in USB** (the
   bootloader's own check, so it works whatever firmware is on it), or, with this firmware
   running, hold them for 3 seconds. The four track lights come on.
3. Open [solderless.engineering](https://solderless.engineering), choose `sp1verb.bin`, flash,
   unplug, plug back in.

The SP-1 has no reset button: if anything ever hangs, the watchdog restarts it within 4 seconds;
if it won't start at all, step 2's plug-in hold still reaches the bootloader.

## Status line

Open the SP-1's USB serial port (any baud rate) for a line a second: `model=`, `f=` fader
readings, `usb=` streaming, `fill=` the input buffer, `under= over=` buffer under/overruns,
`blk=last/max us` the time one 2.67 ms audio block takes (the CPU check: it must stay well under
2667), `hp=` headphones, `gr=` the ceiling's reduction, `flt=` a crash from the last boot.

## CPU and memory

- RAM: about 162 KB of 256 KB, mostly the reverb's delay lines.
- CPU, worst case (any model, with the filter, grit and modulation all on): about 60% of the
  64 MHz Cortex-M4, estimated by `tools/bench_m4.py` (`BENCH_MODEL=0..7` picks the model;
  `BENCH_SWITCH=n` measures a switch to model n, which costs no more). The real figure is the
  status line's `blk=`.

## Building

```bash
make test        # the engine on your computer: checks for every model, and WAVs in build/test
make firmware    # sp1verb.bin; needs ZEPHYR_BASE (Zephyr v4.3.1) and the Zephyr SDK 0.17.4
make bench       # the Cortex-M4 cycle estimate (pip install unicorn capstone)
```

`.github/workflows/build.yml` builds the firmware, checks it loads where the bootloader puts it,
runs the tests, checks every model's CPU estimate stays under 80%, and keeps `sp1verb.bin` and
the WAVs as artifacts.

| Path | |
|---|---|
| `fx/` | the reverb engine: plain C, float, no libm, no heap; runs the same on the host and the SP-1 |
| `test/` | host checks for the engine and each model, and renders |
| `firmware/` | Zephyr application: USB audio (UAC2), I2S, codecs, faders, buttons, lights, power |
| `boards/` | the SP-1 board definition |
| `tools/bench_m4.py` | the CPU estimate |
| `src/`, `index.html` | a browser preview of the controls (separate from the firmware; its presets are not the firmware's models) |

## Credits

- [chattock/sp1-tape-looper](https://github.com/chattock/sp1-tape-looper) (MIT): the SP-1 board
  definition, the USB audio class backport, and the platform code: codec setup, I2S clocking, USB
  feedback, buttons, power and the bootloader rules. Its licence is in
  `firmware/LICENSE-sp1-tape-looper`.
- [timknapen/SP-1-dev](https://github.com/timknapen/SP-1-dev): the SP-1's pinout, peripherals and
  bootloader documentation.
- The tank follows Jon Dattorro's "Effect Design, Part 1" (JAES, 1997).
