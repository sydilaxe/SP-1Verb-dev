---
title: SP-1 Reverb
subtitle: An early-digital reverb with eight models, from a lush hall to a 60s spring tank.
pdf-name: SP-1-Reverb-manual
---

<!-- Keep this manual in step with the firmware: any change to what a control does, a light shows,
     a model, a range, a timing or the flashing steps belongs here too. CI builds it into
     SP-1-Reverb-manual.pdf and attaches it to every release. -->

# Before you start

SP-1 Reverb is replacement firmware for the Teenage Engineering SP-1. It turns the SP-1 into a
reverb with eight models: you play audio into it over USB-C, shape the space with the four
faders, and hear the result on headphones or the built-in speaker.

> **Test build.** This firmware builds, its reverb engine passes its tests, and its platform code
> follows firmware that does run on the SP-1, but it has not yet been run on a real SP-1. Read
> [Flashing and getting back](#flashing-and-getting-back) before you install it: the SP-1 can
> always be returned to other firmware.

## What you need

- A Teenage Engineering SP-1.
- A USB-C cable.
- An audio source that can play to a USB sound card: a computer, phone, tablet or Linux handheld.
- For installing: a computer with Chrome or Edge.

## Quick start

1. Install the firmware (see [Flashing and getting back](#flashing-and-getting-back)).
2. Hold **FUNCTION** for 1.5 seconds to power on.
3. Connect the SP-1 to your computer or phone with USB-C. It appears as a sound card called
   **SP-1 Reverb**. Choose it as the audio output and play something.
4. Plug in headphones, or listen on the speaker.
5. Push fader 1 (**Mix**) up to bring in the reverb, and fader 2 (**Decay**) to make it longer.
6. Press **TRACK 1-4** to try the models.

# The models

Pick a model with the **TRACK** buttons. TRACK *n* picks model *n*; pressing the same button
again flips to model *n* + 4, and again flips back. The SP-1 Reverb starts on **Hall**.

| Button | Model | What it is | Decay sets |
|---|---|---|---|
| TRACK 1 | **Hall** | a large modulated tank: lush and long | 0.6 to 30 s |
| TRACK 2 | **Plate** | dense and bright, builds fast | 0.4 to 10 s |
| TRACK 3 | **Room** | small and short | 0.2 to 3 s |
| TRACK 4 | **Spring** | a 60s tube spring tank | 0.4 to 6 s |
| TRACK 1, again | **Cloud** | huge, sparse and heavily modulated | 2 to 60 s |
| TRACK 2, again | **Shimmer** | an octave up in the feedback, so each echo climbs | 1 to 30 s |
| TRACK 3, again | **Gated** | the 80s drum reverb, cut off after the hit | gate time, 60 to 600 ms |
| TRACK 4, again | **Reverse** | swells up after each hit, then cuts off | swell length, 0.15 to 1 s |

For 1.2 seconds after a model change the track lights show the model: its button's light, steady
for models 1-4 and blinking for models 5-8.

## Hall, Plate and Room

Three tank reverbs after Jon Dattorro's classic design. **Hall** is large, modulated and long.
**Plate** is dense and bright and builds up fast. **Room** is small and short, for space rather
than effect.

## Spring

A 60s tube spring tank in the style of the Fisher Space Expander: thin lows, dark highs, and a
long "drip" on every trip through the springs, with the lows arriving about 12 ms after the
highs. A hard hit shakes the springs and makes them crash; a soft one doesn't. On this model
**Diffusion** sets how much drip you hear.

## Cloud

Huge, sparse and heavily modulated: a scattered ambient wash with decay times up to a minute.

## Shimmer

An octave shift inside the tank's feedback, so every echo climbs an octave above the last. Even
with Decay at the top, the tail always dies away.

## Gated

The 80s drum reverb: dense while the hit lasts, then cut off. **Decay** sets the gate time.

## Reverse

Swells up after each hit, then cuts off, like a reversed reverb. **Decay** sets the swell length.

## Switching models while playing

Switching between **Hall, Plate, Room, Cloud and Shimmer** keeps the tail: what is already ringing
carries on into the new model, which takes over in about 130 ms (Shimmer's octave fades out first,
or in after). Switching to or from **Spring, Gated or Reverse** fades the reverb out and back in
over about 40 ms instead. The dry signal never moves, and switching never clicks.

# The faders

The faders have two pages. Press **FWD (>>)** on the side rocker for page 2 and **RWD (<<)** to
go back to page 1. Page 1 is the same on every model. Page 2 belongs to the model you're playing:
four controls of its own, which change when you pick another model.

| Fader | Page 1 (every model) |
|---|---|
| 1 | **Mix** |
| 2 | **Decay** |
| 3 | **Diffusion** |
| 4 | **Filter** |

## Page 1

**Mix.** Dry at the bottom, all reverb at the top, at constant power so the level holds through
the middle. With Mix at the bottom the output is the input, bit for bit.

**Decay.** The model's reverb time (see [The models](#the-models)). The tail darkens as it
lengthens, like a real space.

**Diffusion.** Scattered, separate echoes at the bottom; a dense, smooth cloud at the top. On
Spring it sets how much drip you hear.

**Filter.** The reverb's tone. The middle fifth of the fader is flat. Moving down sweeps a
low-pass down to 200 Hz; moving up sweeps a high-pass up to 4 kHz. Only the reverb is filtered,
never the dry signal.

## Page 2: the model's own controls

| Model | Fader 1 | Fader 2 | Fader 3 | Fader 4 |
|---|---|---|---|---|
| Hall | Mod depth | Mod rate | Pre-delay | Damping |
| Plate | Density | Damping | Pre-delay | Grit |
| Room | Density | Damping | Pre-delay | Grit |
| Spring | Drive | Crash | Pre-delay | Wobble |
| Cloud | Mod depth | Mod rate | Pre-delay | Damping |
| Shimmer | Octave | Mod depth | Pre-delay | Damping |
| Gated | Threshold | Release | Pre-delay | Grit |
| Reverse | Curve | Threshold | Pre-delay | Damping |

Fader 3 is always Pre-delay. Each model remembers its own page 2: turn up Grit on Plate, switch
to Room and back, and Plate's Grit is where you left it. At their starting positions the page 2
controls leave each model sounding just as described in [The models](#the-models). A control a
model doesn't have stays at that starting position (Hall, for example, has no Grit).

**Mod depth.** How much the tank is chorused. Starts at a quarter.

**Mod rate.** The speed of that movement, 0.2 to 4.5 Hz.

**Pre-delay.** 0 to 150 ms of silence before the reverb starts, to keep the dry hit clear. Starts
at 15 ms.

**Damping.** The tail's top. The middle is the model's own; down darkens the tail, up brightens it.

**Density.** How quickly the echoes thicken into a smooth tail. The middle is the model's own; down
leaves more separate echoes, up smears them faster.

**Grit.** An early-digital converter on the reverb, down to 6 bits. The bit steps follow the
reverb's level, so quiet tails get gritty too instead of cutting off. Starts at the bottom (off).

**Drive** (Spring). The tube driver in front of the springs: clean at the bottom, squashed and
thick when played hard at the top. The middle is the model's own.

**Crash** (Spring). How hard the springs rattle when a hard hit shakes the tank: none at the
bottom, twice the model's own at the top. The middle is the model's own.

**Wobble** (Spring). The springs' slow pitch wobble: none at the bottom.

**Octave** (Shimmer). How much octave goes back into the tank: none at the bottom (a plain, long
hall), the full climbing shimmer at the top, where it starts.

**Threshold** (Gated, Reverse). How loud the input must be to open the gate, or start the swell:
-60 dBFS at the bottom to -20 dBFS at the top, -40 dBFS in the middle where it starts. Raise it
so only the hits trigger it, not the quieter sounds around them.

**Release** (Gated). How fast the gate closes at the end: 1 ms (a hard cut) at the bottom to
64 ms at the top, 8 ms in the middle where it starts.

**Curve** (Reverse). The swell's shape: a straight ramp at the bottom, a square curve in the
middle where it starts, and at the top a cube: quiet for longer, then a rush at the end.

## Pick-up

When you change page, the faders are usually not where that page's values are. So nothing jumps,
a parameter keeps its value until you move its fader to that value (or sweep past it). Its track
light blinks until the fader has picked it up. The same happens on page 2 when you pick another
model, since its page 2 has its own values.

## The safety ceiling

After the mix, a safety limiter at -1 dBFS (1 ms look-ahead) is always on. It only acts on overs,
to protect the speaker and your ears, and leaves everything below the ceiling untouched.

# Controls

| Control | Action |
|---|---|
| Faders 1-4 | page 1: Mix, Decay, Diffusion, Filter; page 2: the model's own four (see [Page 2](#page-2-the-models-own-controls)) |
| FWD (>>) | go to page 2 |
| RWD (<<) | go to page 1 |
| TRACK 1-4 | pick a model; the same button again flips to model + 4 |
| PLAY | bypass: the reverb glides out and back |
| VOL + / VOL - | output volume, 3 dB steps |
| FUNCTION, hold 1.5 s | power on |
| FUNCTION, hold 2.5 s | power off |
| TRACK 1 + TRACK 4, hold 3 s | restart into the bootloader, for flashing |

## Bypass

**PLAY** glides the reverb out, and pressed again, back in. The safety ceiling stays on. In bypass
the status lights breathe slowly.

## Volume

**VOL +** and **VOL -** change the output volume in 3 dB steps, from 0 dB down to -60 dB. The
SP-1 Reverb starts at -6 dB. For a second after a change the status lights show the volume.

## Headphones and speaker

Plug in headphones and the speaker turns off. Unplug them and the speaker comes back.

# Lights

**Track lights** show the faders of the page you're on (for Filter, its distance from the
centre). A light blinks while its fader is waiting to pick up after a page change (or, on page 2, a model
change). For 1.2 s after
a model change they show the model instead: its button's light, steady for models 1-4 and
blinking for 5-8.

**Status lights** normally meter the safety ceiling's gain reduction: one light from 0.5 dB, two
from 2 dB, three from 4 dB and four from 8 dB, the fourth blinking while on page 2. They switch to
other information for a moment:

- for 0.6 s after FWD or RWD: the page (left pair = page 1, right pair = page 2);
- for 1 s after VOL + or VOL -: the volume;
- in bypass: all four breathe slowly.

# Power and charging

- **Power on:** hold **FUNCTION** for 1.5 seconds. The status lights fill up as you hold.
- **Power off:** hold **FUNCTION** for 2.5 seconds.
- **Charging:** while the SP-1 is off and plugged in, the status lights show the battery level,
  with the top lit light blinking while it charges.
- **Settings:** the model, page, page 2 settings and volume are not stored. Each time you power
  on, the SP-1 Reverb starts on Hall, page 1, with every model's page 2 at its starting positions,
  the volume at -6 dB and the faders where they are.

If the firmware ever hangs, a watchdog restarts it within 4 seconds and it comes straight back
up.

# Flashing and getting back

The SP-1 is flashed with the [Solderless updater](https://solderless.engineering) in Chrome or
Edge. Download `sp1verb.bin` from the release page first.

1. Connect the SP-1 to the computer with a USB-C cable.
2. Put it in firmware-loading mode: **hold TRACK 1 + TRACK 4 while you plug in USB**. This is the
   bootloader's own check, so it works whatever firmware is on the SP-1. With SP-1 Reverb already
   running, you can instead hold TRACK 1 + TRACK 4 for 3 seconds. The four track lights come on.
3. In the updater, choose `sp1verb.bin` and flash it.
4. Unplug the SP-1 and plug it back in.

**Going back** to other firmware uses the same steps with that firmware's `.bin` file. The SP-1
has no reset button: if it won't start at all, the plug-in hold in step 2 still reaches the
bootloader.

# Troubleshooting

| Problem | Try |
|---|---|
| No sound | Check that **SP-1 Reverb** is selected as the output on your computer or phone and that the volume is up (VOL +). |
| No reverb | Push up Mix (fader 1, page 1), and check PLAY bypass is off (status lights not breathing). |
| It doesn't appear as a sound card | Use a USB-C cable that carries data, not a charge-only one. Power the SP-1 on with FUNCTION. |
| A fader doesn't respond after a page change | It is waiting to pick up: move it to where the light's value is, or sweep it past. |
| The reverb sounds thin or dull | Check the Filter (fader 4, page 1) is in its flat middle fifth, and on page 2 that Damping is not at the bottom and Grit (Plate, Room, Gated) is down. |
| Gated or Reverse doesn't trigger | Lower Threshold (page 2): the input may be quieter than it. |
| Crackles or dropouts | Send the status line (below). |
| It won't start | Hold TRACK 1 + TRACK 4 while plugging in USB and reflash. |

## The status line

For a detailed report, open the SP-1's USB serial port in any serial terminal (any baud rate). It
prints one line a second:

| Field | Meaning |
|---|---|
| `model=` | the model, 0 (Hall) to 7 (Reverse) |
| `f=` | fader readings |
| `usb=` | whether audio is streaming |
| `fill=` | how full the input buffer is |
| `under=` `over=` | input buffer underruns and overruns |
| `blk=last/max us` | how long one 2.67 ms audio block takes; must stay well under 2667 |
| `hp=` | headphones detected |
| `gr=` | the safety ceiling's gain reduction |
| `flt=` | a crash from the previous boot, if any |

If something is wrong on the hardware, this line is the thing to send with your report.

# Specifications

| | |
|---|---|
| Audio in | USB-C, class-compliant USB audio (UAC2), 48 kHz stereo, 16- or 24-bit (the host picks) |
| Audio out | headphones or speaker, 48 kHz / 24-bit |
| Processing | 32-bit floating point at 48 kHz |
| Latency | 48 samples (1 ms); with Mix at the bottom the output is the input, bit for bit |
| Models | Hall, Plate, Room, Spring, Cloud, Shimmer, Gated, Reverse |
| Safety ceiling | -1 dBFS, 1 ms look-ahead, always on |
| Volume | 0 to -60 dB in 3 dB steps |
| CPU | about 60% of the 64 MHz Cortex-M4 worst case (estimated) |
| RAM | about 162 KB of 256 KB, mostly the reverb's delay lines |

# Credits

- [chattock/sp1-tape-looper](https://github.com/chattock/sp1-tape-looper) (MIT): the SP-1 board
  definition, the USB audio class backport, and the platform code this firmware follows.
- [timknapen/SP-1-dev](https://github.com/timknapen/SP-1-dev): the SP-1's pinout, peripherals and
  bootloader documentation.
- The tank follows Jon Dattorro's "Effect Design, Part 1" (JAES, 1997).

SP-1 Reverb is not made or endorsed by Teenage Engineering.
