SP-1 Reverb test build for the Teenage Engineering SP-1: a reverb with eight models. Audio in over USB-C
(a class-compliant sound card, 48 kHz / 24-bit), out on headphones or the speaker.

**Not yet tried on a real SP-1.** It builds, the engine passes its tests, and the platform code is the same
as SP-1FX's, but this binary has not been on hardware yet.

**Flash:** open the [Solderless updater](https://solderless.engineering) in Chrome or Edge, hold **TRACK 1 +
TRACK 4** while you plug the SP-1 in (the four track lights come on), choose `sp1verb.bin`, flash, then unplug
and plug back in. The same plug-in hold always gets back to the bootloader, whatever firmware is on it.

**Models** (TRACK n picks model n; the same button again flips to n + 4): Hall, Plate, Room, Spring /
Cloud, Shimmer, Gated, Reverse.

**Faders:** page 1 Mix, Decay, Diffusion, Filter (the reverb's tone; centre fifth flat). FWD goes to page 2:
Mod depth, Mod rate, Pre-delay, Grit; RWD back to page 1. A parameter picks up when its fader reaches it, so
nothing jumps.

**Other buttons:** PLAY bypasses the reverb, VOL + / - set the volume (3 dB steps), hold FUNCTION to power on
(1.5 s) or off (2.5 s), hold TRACK 1 + 4 for 3 s to restart into the bootloader.

**The sound:** full bandwidth: 48 kHz / 24-bit in and out, 32-bit float inside. With Mix at the bottom the
output is the input, bit for bit, 1 ms later. A -1 dBFS safety ceiling only acts on overs.

**If something is wrong**, open the SP-1's USB serial port: a status line a second shows the model, the
faders, the USB stream, the audio load (`blk=`, microseconds per 2.67 ms block) and the last crash.
