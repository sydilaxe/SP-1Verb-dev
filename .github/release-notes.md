SP-1 Reverb test build for the Teenage Engineering SP-1: a reverb with eight models. Audio in over USB-C
(a class-compliant sound card, 48 kHz, 16- or 24-bit: the host picks, so Linux handhelds such as the TrimUI
Brick work too), out on headphones or the speaker.

**Not yet tried on a real SP-1.** It builds, the engine passes its tests, and the platform code is the same
as SP-1FX's, but this binary has not been on hardware yet.

**Flash:** open the [Solderless updater](https://solderless.engineering) in Chrome or Edge, hold **TRACK 1 +
TRACK 4** while you plug the SP-1 in (the four track lights come on), choose `sp1verb.bin`, flash, then unplug
and plug back in. The same plug-in hold always gets back to the bootloader, whatever firmware is on it.

**Models** (TRACK n picks model n; the same button again flips to n + 4): Hall, Plate, Room, Spring /
Cloud, Shimmer, Gated, Reverse.

**Faders:** page 1 Mix, Decay, Diffusion, Filter (the reverb's tone; centre fifth flat), the same on every
model. FWD on the side rocker goes to page 2, the playing model's own four controls (each model remembers its
own; fader 3 is always Pre-delay); RWD back to page 1. A parameter picks up when its fader reaches it, so
nothing jumps.

**New: page 2 per model.** Hall and Cloud: Mod depth, Mod rate, Pre-delay, Damping. Plate and Room: Density,
Damping, Pre-delay, Grit. Spring: Drive, Crash, Pre-delay, Wobble. Shimmer: Octave, Mod depth, Pre-delay,
Damping. Gated: Threshold, Release, Pre-delay, Grit. Reverse: Curve, Threshold, Pre-delay, Damping. At their
starting positions every model sounds as before.

**Other buttons:** PLAY bypasses the reverb, VOL + / - set the volume (3 dB steps), hold FUNCTION to power on
(1.5 s) or off (2.5 s), hold TRACK 1 + 4 for 3 s to restart into the bootloader.

**The sound:** full bandwidth: 48 kHz, 16- or 24-bit in, 24-bit out, 32-bit float inside. With Mix at the bottom the
output is the input, bit for bit, 1 ms later. A -1 dBFS safety ceiling only acts on overs.

**If something is wrong**, open the SP-1's USB serial port: a status line a second shows the model, the
faders, the USB stream, the audio load (`blk=`, microseconds per 2.67 ms block) and the last crash.

**The full user manual** is attached below as `SP-1-Reverb-manual.pdf`.
