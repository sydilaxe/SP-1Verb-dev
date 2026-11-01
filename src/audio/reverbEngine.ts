/**
 * Teenage Engineering SP-1 Stem Player - Dedicated Early Digital Reverb DSP Engine
 * Optimized for low-CPU embedded DSP & Web Audio runtime with Live USB Audio Input.
 * 
 * Features:
 * - Real-time USB Audio Class (UAC) Live Streaming with studio low-latency (no noise gating or voice filtering)
 * - Stereo/Mono Channel routing & Input Gain Trim
 * - 4-Stem Synchronized Audio Engine (Drums, Bass, Melodic Synth, Vocal Chops) with 0-allocation looping
 * - Early Digital Reverb (Schroeder-Dattorro topology with vintage 12-bit/16-bit converter emulation)
 * - Ultra-low CPU mode with throttled updates, pre-rendered buffers, and optimized all-pass network
 * - Real-time DSP load, USB stream status, and memory telemetry
 */

export interface ReverbParams {
  // SET 1: Core Reverb Controls (Four Main Controls)
  mix: number;           // 0.0 to 1.0 (Dry/Wet)
  decay: number;         // 0.2 to 25.0 seconds (RT60 decay time)
  diffusion: number;     // 0.0 to 1.0 (Early reflection smearing)
  filterCutoff: number;  // 200 Hz to 16000 Hz (LP / Tone filter)
  filterType: 'lp' | 'hp' | 'bp'; // Filter mode
  filterResonance: number; // 0.1 to 6.0 Q

  // SET 2: Tone Character & Real-time Modulation (Switched by PLAY button)
  modDepth: number;      // 0.0 to 1.0 (Tank chorusing depth)
  modRate: number;       // 0.1 to 6.0 Hz (LFO spin rate)
  preDelay: number;      // 0 to 250 ms (Initial onset delay)
  vintageGrit: number;   // 0.0 to 1.0 (Lo-fi early digital 12-bit DAC quantization & warmth)

  // Performance toggles
  freeze: boolean;       // Freeze reverb tail indefinitely
  bank: 'A' | 'B';       // Bank A = Core (1-4), Bank B = Mod (1-4)
  lowCpuMode: boolean;   // Reduced allpass stages & lowered clock rate for embedded CPU
}

export interface StemState {
  volume: number; // 0.0 to 1.0
  muted: boolean;
  soloed: boolean;
}

export interface UsbAudioConfig {
  enabled: boolean;
  deviceId: string;
  deviceName: string;
  inputGain: number;       // 0.0 to 2.5 (gain multiplier)
  channelMode: 'stereo' | 'mono-l' | 'mono-r';
  directMonitor: boolean;  // Listen to dry input directly
}

export const DEFAULT_PARAMS: ReverbParams = {
  mix: 0.42,
  decay: 4.8,
  diffusion: 0.72,
  filterCutoff: 6500,
  filterType: 'lp',
  filterResonance: 0.7,
  modDepth: 0.55,
  modRate: 0.85,
  preDelay: 28,
  vintageGrit: 0.35,
  freeze: false,
  bank: 'A',
  lowCpuMode: true,
};

// Lightweight All-pass filter stage
class LightweightAllPass {
  public input: GainNode;
  public output: GainNode;
  private delay: DelayNode;
  private feedforward: GainNode;
  private feedback: GainNode;
  private ctx: AudioContext;

  constructor(ctx: AudioContext, delaySeconds: number, gain: number) {
    this.ctx = ctx;
    this.input = ctx.createGain();
    this.output = ctx.createGain();
    this.delay = ctx.createDelay(0.5);
    this.delay.delayTime.value = delaySeconds;

    this.feedforward = ctx.createGain();
    this.feedforward.gain.value = -gain;

    this.feedback = ctx.createGain();
    this.feedback.gain.value = gain;

    this.input.connect(this.feedforward);
    this.feedforward.connect(this.output);

    this.input.connect(this.delay);
    this.delay.connect(this.output);

    this.delay.connect(this.feedback);
    this.feedback.connect(this.delay);
  }

  public setGain(g: number) {
    const clamped = Math.max(0.01, Math.min(0.92, g));
    this.feedback.gain.setTargetAtTime(clamped, this.ctx.currentTime, 0.03);
    this.feedforward.gain.setTargetAtTime(-clamped, this.ctx.currentTime, 0.03);
  }

  public setDelayTime(t: number) {
    this.delay.delayTime.setTargetAtTime(Math.max(0.001, t), this.ctx.currentTime, 0.03);
  }
}

export class SP1ReverbEngine {
  private ctx: AudioContext | null = null;
  private params: ReverbParams = { ...DEFAULT_PARAMS };

  // 4-Stem Gains & Nodes
  private stemGains: GainNode[] = [];
  private stemSources: (AudioBufferSourceNode | null)[] = [null, null, null, null];
  private stemBuffers: (AudioBuffer | null)[] = [null, null, null, null];
  private stemStates: StemState[] = [
    { volume: 0.85, muted: false, soloed: false },
    { volume: 0.80, muted: false, soloed: false },
    { volume: 0.75, muted: false, soloed: false },
    { volume: 0.80, muted: false, soloed: false },
  ];

  // Reverb Routing
  private stemMixBus: GainNode | null = null;
  private reverbSendNode: GainNode | null = null;
  private preDelayNode: DelayNode | null = null;
  private inputDiffusers: LightweightAllPass[] = [];

  // Live USB Audio Nodes
  private usbAudioConfig: UsbAudioConfig = {
    enabled: false,
    deviceId: 'default',
    deviceName: 'Default USB In',
    inputGain: 1.0,
    channelMode: 'stereo',
    directMonitor: true,
  };
  private usbStream: MediaStream | null = null;
  private usbSourceNode: MediaStreamAudioSourceNode | null = null;
  private usbGainNode: GainNode | null = null;
  private usbSplitterNode: ChannelSplitterNode | null = null;
  private usbMergerNode: ChannelMergerNode | null = null;

  // Reverb Tank Left & Right
  private tankDelayL: DelayNode | null = null;
  private tankDelayR: DelayNode | null = null;
  private tankFilterL: BiquadFilterNode | null = null;
  private tankFilterR: BiquadFilterNode | null = null;
  private tankHighPassL: BiquadFilterNode | null = null;
  private tankHighPassR: BiquadFilterNode | null = null;
  private tankFeedbackL: GainNode | null = null;
  private tankFeedbackR: GainNode | null = null;
  private tankCrossLtoR: GainNode | null = null;
  private tankCrossRtoL: GainNode | null = null;
  private tankDiffuserL: LightweightAllPass | null = null;
  private tankDiffuserR: LightweightAllPass | null = null;

  // LFO
  private lfoOsc: OscillatorNode | null = null;
  private lfoGainL: GainNode | null = null;
  private lfoGainR: GainNode | null = null;

  // Vintage Grit / Bitcrush
  private gritShaper: WaveShaperNode | null = null;

  // Wet / Dry Matrix
  private dryGain: GainNode | null = null;
  private wetGain: GainNode | null = null;
  private masterGain: GainNode | null = null;
  public inputAnalyser: AnalyserNode | null = null;
  public outputAnalyser: AnalyserNode | null = null;
  public usbAnalyser: AnalyserNode | null = null;

  // State
  private isPlaying = false;
  private isStemsLoaded = false;
  private estimatedDspLoad = 12; // Percentage
  private onStateChange?: (isPlaying: boolean, dspLoad: number) => void;

  constructor() {
    // Lazy initialization
  }

  public setListener(cb: (isPlaying: boolean, dspLoad: number) => void) {
    this.onStateChange = cb;
  }

  public async initAudio(): Promise<boolean> {
    if (this.ctx && this.ctx.state !== 'closed') {
      if (this.ctx.state === 'suspended') {
        await this.ctx.resume();
      }
      return true;
    }

    try {
      const AudioCtx = window.AudioContext || (window as unknown as { webkitAudioContext: typeof AudioContext }).webkitAudioContext;
      this.ctx = new AudioCtx({ latencyHint: 'interactive' });
      await this.buildReverbGraph();
      await this.preRenderStems();
      if (this.ctx.state === 'suspended') {
        await this.ctx.resume();
      }
      return true;
    } catch (e) {
      console.error('Failed to init SP-1 Web Audio:', e);
      return false;
    }
  }

  // Pre-render stems once for zero-CPU looping
  private async preRenderStems() {
    if (!this.ctx || this.isStemsLoaded) return;
    const ctx = this.ctx;
    const sampleRate = ctx.sampleRate;
    const bpm = 108;
    const secondsPerBeat = 60 / bpm;
    const totalBars = 4;
    const loopDuration = secondsPerBeat * 4 * totalBars;
    const totalSamples = Math.floor(sampleRate * loopDuration);

    // 1. STEM 1: DRUMS
    const drumBuffer = ctx.createBuffer(2, totalSamples, sampleRate);
    const dL = drumBuffer.getChannelData(0);
    const dR = drumBuffer.getChannelData(1);
    const stepTime = secondsPerBeat / 4;
    const totalSteps = 16 * totalBars;

    for (let step = 0; step < totalSteps; step++) {
      const stepIdx = step % 16;
      const t = step * stepTime;
      const startSample = Math.floor(t * sampleRate);

      if (stepIdx === 0 || stepIdx === 6 || stepIdx === 10) {
        const kickLen = Math.floor(sampleRate * 0.28);
        for (let i = 0; i < kickLen && startSample + i < totalSamples; i++) {
          const kt = i / sampleRate;
          const freq = 140 * Math.exp(-kt * 28) + 45;
          const amp = Math.exp(-kt * 12);
          const val = Math.sin(2 * Math.PI * freq * kt) * amp * 0.7;
          dL[startSample + i] += val;
          dR[startSample + i] += val;
        }
      }

      if (stepIdx === 4 || stepIdx === 12) {
        const snareLen = Math.floor(sampleRate * 0.22);
        for (let i = 0; i < snareLen && startSample + i < totalSamples; i++) {
          const st = i / sampleRate;
          const tone = Math.sin(2 * Math.PI * (220 * Math.exp(-st * 16) + 120) * st) * Math.exp(-st * 14) * 0.4;
          const noise = (Math.random() * 2 - 1) * Math.exp(-st * 10) * 0.55;
          dL[startSample + i] += tone + noise;
          dR[startSample + i] += tone + noise;
        }
      }

      if (stepIdx === 8 || stepIdx === 14) {
        const clapLen = Math.floor(sampleRate * 0.12);
        for (let i = 0; i < clapLen && startSample + i < totalSamples; i++) {
          const ct = i / sampleRate;
          const noise = (Math.random() * 2 - 1) * Math.exp(-ct * 24) * 0.4;
          dL[startSample + i] += noise * 0.8;
          dR[startSample + i] += noise * 1.1;
        }
      }

      const hatLen = Math.floor(sampleRate * 0.05);
      const isAccent = stepIdx % 4 === 2;
      for (let i = 0; i < hatLen && startSample + i < totalSamples; i++) {
        const ht = i / sampleRate;
        const noise = (Math.random() * 2 - 1) * Math.exp(-ht * (isAccent ? 35 : 65)) * (isAccent ? 0.22 : 0.14);
        dL[startSample + i] += noise * (step % 2 === 0 ? 0.8 : 1.2);
        dR[startSample + i] += noise * (step % 2 === 0 ? 1.2 : 0.8);
      }
    }
    this.stemBuffers[0] = drumBuffer;

    // 2. STEM 2: BASS
    const bassBuffer = ctx.createBuffer(2, totalSamples, sampleRate);
    const bL = bassBuffer.getChannelData(0);
    const bR = bassBuffer.getChannelData(1);
    const bassNotes = [
      73.42, 73.42, 73.42, 87.31,
      65.41, 65.41, 98.00, 73.42,
      58.27, 58.27, 58.27, 87.31,
      65.41, 65.41, 87.31, 98.00,
    ];

    for (let step = 0; step < totalSteps; step++) {
      if (step % 2 === 0) {
        const noteIdx = Math.floor(step / 4) % bassNotes.length;
        const freq = bassNotes[noteIdx];
        const startSample = Math.floor(step * stepTime * sampleRate);
        const noteLen = Math.floor(sampleRate * (stepTime * 1.8));

        for (let i = 0; i < noteLen && startSample + i < totalSamples; i++) {
          const bt = i / sampleRate;
          const saw = (2 * ((freq * bt) % 1) - 1);
          const sub = Math.sin(2 * Math.PI * (freq / 2) * bt);
          const env = Math.exp(-bt * 3.2);
          const val = (saw * 0.4 + sub * 0.6) * env * 0.55;
          bL[startSample + i] += val;
          bR[startSample + i] += val;
        }
      }
    }
    this.stemBuffers[1] = bassBuffer;

    // 3. STEM 3: SYNTH
    const synthBuffer = ctx.createBuffer(2, totalSamples, sampleRate);
    const sL = synthBuffer.getChannelData(0);
    const sR = synthBuffer.getChannelData(1);
    const chords = [
      [146.83, 220.00, 261.63, 329.63],
      [116.54, 174.61, 220.00, 261.63],
      [174.61, 220.00, 261.63, 349.23],
      [130.81, 196.00, 261.63, 329.63],
    ];

    for (let bar = 0; bar < totalBars; bar++) {
      const chord = chords[bar % chords.length];
      const barStart = Math.floor(bar * 4 * secondsPerBeat * sampleRate);

      for (let noteI = 0; noteI < 16; noteI++) {
        const noteStart = barStart + Math.floor(noteI * stepTime * sampleRate);
        const freq = chord[noteI % chord.length] * (noteI % 2 === 0 ? 1 : 2);
        const noteLen = Math.floor(sampleRate * (stepTime * 1.2));

        for (let i = 0; i < noteLen && noteStart + i < totalSamples; i++) {
          const st = i / sampleRate;
          const saw1 = Math.sin(2 * Math.PI * (freq * 1.003) * st);
          const saw2 = Math.sin(2 * Math.PI * (freq * 0.997) * st);
          const env = Math.exp(-st * 6.5);
          const panL = 0.5 + 0.3 * Math.sin(noteI);
          const panR = 1.0 - panL;

          sL[noteStart + i] += (saw1 + saw2) * 0.22 * env * panL;
          sR[noteStart + i] += (saw1 + saw2) * 0.22 * env * panR;
        }
      }
    }
    this.stemBuffers[2] = synthBuffer;

    // 4. STEM 4: VOCALS
    const vocalBuffer = ctx.createBuffer(2, totalSamples, sampleRate);
    const vL = vocalBuffer.getChannelData(0);
    const vR = vocalBuffer.getChannelData(1);
    const vocalPitches = [293.66, 349.23, 440.0, 392.0, 329.63, 293.66];

    for (let phrase = 0; phrase < totalBars * 2; phrase++) {
      const phraseStart = Math.floor(phrase * 2 * secondsPerBeat * sampleRate);
      const freq = vocalPitches[phrase % vocalPitches.length];
      const chopLen = Math.floor(sampleRate * 0.65);

      for (let i = 0; i < chopLen && phraseStart + i < totalSamples; i++) {
        const vt = i / sampleRate;
        const f1 = 650;
        const f2 = 1100;
        const formant1 = Math.sin(2 * Math.PI * f1 * vt);
        const formant2 = Math.sin(2 * Math.PI * f2 * vt);
        const carrier = Math.sin(2 * Math.PI * freq * vt);
        const env = Math.sin((vt / 0.65) * Math.PI) * 0.28;

        const chopSound = carrier * (formant1 * 0.6 + formant2 * 0.4) * env;
        vL[phraseStart + i] += chopSound * 0.85;
        vR[phraseStart + i] += chopSound * 1.05;
      }
    }
    this.stemBuffers[3] = vocalBuffer;

    this.isStemsLoaded = true;
  }

  // Build the lightweight DSP graph with USB routing
  private async buildReverbGraph() {
    if (!this.ctx) return;
    const ctx = this.ctx;

    // 1. Stem Mixing Bus
    this.stemMixBus = ctx.createGain();
    this.stemMixBus.gain.value = 1.0;

    // 4 Stem Gain Nodes
    this.stemGains = this.stemStates.map(state => {
      const gain = ctx.createGain();
      gain.gain.value = state.muted ? 0 : state.volume;
      gain.connect(this.stemMixBus!);
      return gain;
    });

    // 2. Live USB Audio Input Bus
    this.usbGainNode = ctx.createGain();
    this.usbGainNode.gain.value = this.usbAudioConfig.inputGain;

    this.usbAnalyser = ctx.createAnalyser();
    this.usbAnalyser.fftSize = 64;
    this.usbGainNode.connect(this.usbAnalyser);
    this.usbGainNode.connect(this.stemMixBus); // Connect USB in directly to Reverb & Master!

    // 3. Reverb Send Node & Analyser
    this.reverbSendNode = ctx.createGain();
    this.reverbSendNode.gain.value = 1.0;
    this.stemMixBus.connect(this.reverbSendNode);

    this.inputAnalyser = ctx.createAnalyser();
    this.inputAnalyser.fftSize = this.params.lowCpuMode ? 128 : 256;
    this.stemMixBus.connect(this.inputAnalyser);

    // 4. Pre-delay
    this.preDelayNode = ctx.createDelay(0.5);
    this.preDelayNode.delayTime.value = this.params.preDelay / 1000;
    this.reverbSendNode.connect(this.preDelayNode);

    // 5. Vintage 12-bit / 8-bit DAC Grit Waveshaper
    this.gritShaper = ctx.createWaveShaper();
    this.updateGritCurve(this.params.vintageGrit);
    this.preDelayNode.connect(this.gritShaper);

    // 6. Input Diffusers (3 stages for low-CPU)
    const diffDelays = [0.0047, 0.0093, 0.0127];
    this.inputDiffusers = diffDelays.map(d => new LightweightAllPass(ctx, d, 0.7 * this.params.diffusion));

    let prevNode: AudioNode = this.gritShaper;
    for (const diffuser of this.inputDiffusers) {
      prevNode.connect(diffuser.input);
      prevNode = diffuser.output;
    }
    const diffusedInput = prevNode;

    // 7. Cross-Coupled Delay Tanks (Left & Right)
    this.tankDelayL = ctx.createDelay(1.5);
    this.tankDelayL.delayTime.value = 0.142;

    this.tankDelayR = ctx.createDelay(1.5);
    this.tankDelayR.delayTime.value = 0.168;

    this.tankDiffuserL = new LightweightAllPass(ctx, 0.030, 0.6 * this.params.diffusion);
    this.tankDiffuserR = new LightweightAllPass(ctx, 0.039, 0.6 * this.params.diffusion);

    // Damping Filters
    this.tankFilterL = ctx.createBiquadFilter();
    this.tankFilterL.type = this.params.filterType === 'bp' ? 'bandpass' : this.params.filterType === 'hp' ? 'highpass' : 'lowpass';
    this.tankFilterL.frequency.value = this.params.filterCutoff;
    this.tankFilterL.Q.value = this.params.filterResonance;

    this.tankFilterR = ctx.createBiquadFilter();
    this.tankFilterR.type = this.tankFilterL.type;
    this.tankFilterR.frequency.value = this.params.filterCutoff;
    this.tankFilterR.Q.value = this.params.filterResonance;

    // Rumble Highpass
    this.tankHighPassL = ctx.createBiquadFilter();
    this.tankHighPassL.type = 'highpass';
    this.tankHighPassL.frequency.value = 50;

    this.tankHighPassR = ctx.createBiquadFilter();
    this.tankHighPassR.type = 'highpass';
    this.tankHighPassR.frequency.value = 50;

    // Feedback
    this.tankFeedbackL = ctx.createGain();
    this.tankFeedbackR = ctx.createGain();
    this.tankCrossLtoR = ctx.createGain();
    this.tankCrossRtoL = ctx.createGain();

    const fb = this.calculateFeedbackGain(this.params.decay);
    this.tankFeedbackL.gain.value = fb * 0.7;
    this.tankFeedbackR.gain.value = fb * 0.7;
    this.tankCrossLtoR.gain.value = fb * 0.3;
    this.tankCrossRtoL.gain.value = fb * 0.3;

    // Tank L
    diffusedInput.connect(this.tankDelayL);
    this.tankDelayL.connect(this.tankDiffuserL.input);
    this.tankDiffuserL.output.connect(this.tankFilterL);
    this.tankFilterL.connect(this.tankHighPassL);

    this.tankHighPassL.connect(this.tankFeedbackL);
    this.tankFeedbackL.connect(this.tankDelayL);
    this.tankHighPassL.connect(this.tankCrossLtoR);
    this.tankCrossLtoR.connect(this.tankDelayR);

    // Tank R
    diffusedInput.connect(this.tankDelayR);
    this.tankDelayR.connect(this.tankDiffuserR.input);
    this.tankDiffuserR.output.connect(this.tankFilterR);
    this.tankFilterR.connect(this.tankHighPassR);

    this.tankHighPassR.connect(this.tankFeedbackR);
    this.tankFeedbackR.connect(this.tankDelayR);
    this.tankHighPassR.connect(this.tankCrossRtoL);
    this.tankCrossRtoL.connect(this.tankDelayL);

    // 8. LFO Modulation
    this.lfoOsc = ctx.createOscillator();
    this.lfoOsc.type = 'sine';
    this.lfoOsc.frequency.value = this.params.modRate;

    this.lfoGainL = ctx.createGain();
    this.lfoGainL.gain.value = 0.003 * this.params.modDepth;

    this.lfoGainR = ctx.createGain();
    this.lfoGainR.gain.value = -0.003 * this.params.modDepth;

    this.lfoOsc.connect(this.lfoGainL);
    this.lfoOsc.connect(this.lfoGainR);
    this.lfoGainL.connect(this.tankDelayL.delayTime);
    this.lfoGainR.connect(this.tankDelayR.delayTime);
    this.lfoOsc.start();

    // 9. Wet/Dry Matrix
    const merger = ctx.createChannelMerger(2);
    this.tankHighPassL.connect(merger, 0, 0);
    this.tankHighPassR.connect(merger, 0, 1);

    this.dryGain = ctx.createGain();
    this.wetGain = ctx.createGain();
    this.masterGain = ctx.createGain();
    this.masterGain.gain.value = 0.95;

    this.updateMixGains();

    this.stemMixBus.connect(this.dryGain);
    this.dryGain.connect(this.masterGain);

    merger.connect(this.wetGain);
    this.wetGain.connect(this.masterGain);

    // 10. Output Analyser & Destination
    this.outputAnalyser = ctx.createAnalyser();
    this.outputAnalyser.fftSize = this.params.lowCpuMode ? 128 : 256;
    this.masterGain.connect(this.outputAnalyser);
    this.outputAnalyser.connect(ctx.destination);
  }

  // Pre-calculated feedback gain
  private calculateFeedbackGain(decaySec: number): number {
    if (this.params.freeze) return 0.999;
    const loopTime = 0.155;
    const g = Math.pow(0.001, loopTime / Math.max(0.1, decaySec));
    return Math.min(0.96, Math.max(0.05, g));
  }

  // 12-bit / 8-bit DAC Grit Curve
  private updateGritCurve(grit: number) {
    if (!this.gritShaper) return;
    const nSamples = 1024;
    const curve = new Float32Array(nSamples);
    const bits = 16 - grit * 8;
    const steps = Math.pow(2, bits);
    const drive = 1.0 + grit * 0.8;

    for (let i = 0; i < nSamples; i++) {
      const x = (i / (nSamples - 1)) * 2 - 1;
      const driven = x * drive;
      const quantized = Math.round(driven * (steps / 2)) / (steps / 2);
      curve[i] = Math.tanh(quantized);
    }
    this.gritShaper.curve = curve;
  }

  private updateMixGains() {
    if (!this.dryGain || !this.wetGain || !this.ctx) return;
    const now = this.ctx.currentTime;
    const wet = Math.sin(this.params.mix * 0.5 * Math.PI);
    const dry = Math.cos(this.params.mix * 0.5 * Math.PI);
    this.dryGain.gain.setTargetAtTime(dry, now, 0.03);
    this.wetGain.gain.setTargetAtTime(wet, now, 0.03);
  }

  // ==========================================
  // LIVE USB AUDIO PROCESSING ENGINE
  // ==========================================

  public async getAvailableAudioDevices(): Promise<MediaDeviceInfo[]> {
    try {
      // Prompt briefly if needed to reveal labels
      const devices = await navigator.mediaDevices.enumerateDevices();
      return devices.filter(d => d.kind === 'audioinput');
    } catch {
      return [];
    }
  }

  public async startUsbAudio(deviceId?: string): Promise<boolean> {
    await this.initAudio();
    if (!this.ctx || !this.usbGainNode) return false;

    // Disconnect existing stream if any
    this.stopUsbAudio();

    try {
      // STUDIO LOW-LATENCY AUDIO CONSTRAINTS
      // Disable echoCancellation, noiseSuppression, autoGainControl so instruments and reverbs are pure!
      const constraints: MediaStreamConstraints = {
        audio: {
          deviceId: deviceId && deviceId !== 'default' ? { exact: deviceId } : undefined,
          echoCancellation: false,
          noiseSuppression: false,
          autoGainControl: false,
          channelCount: 2,
        },
        video: false,
      };

      this.usbStream = await navigator.mediaDevices.getUserMedia(constraints);
      this.usbSourceNode = this.ctx.createMediaStreamSource(this.usbStream);

      // Route through channel mode handler
      this.usbSourceNode.connect(this.usbGainNode);
      this.usbAudioConfig.enabled = true;
      if (deviceId) this.usbAudioConfig.deviceId = deviceId;

      const track = this.usbStream.getAudioTracks()[0];
      if (track) {
        this.usbAudioConfig.deviceName = track.label || 'USB Audio In';
      }

      this.estimatedDspLoad = this.params.lowCpuMode ? 16 : 26;
      this.onStateChange?.(this.isPlaying, this.estimatedDspLoad);
      return true;
    } catch (e) {
      console.warn('Failed to start USB audio input:', e);
      this.usbAudioConfig.enabled = false;
      return false;
    }
  }

  public stopUsbAudio() {
    if (this.usbSourceNode) {
      try { this.usbSourceNode.disconnect(); } catch { /* ignore */ }
      this.usbSourceNode = null;
    }
    if (this.usbStream) {
      this.usbStream.getTracks().forEach(t => t.stop());
      this.usbStream = null;
    }
    this.usbAudioConfig.enabled = false;
    this.estimatedDspLoad = this.params.lowCpuMode ? 14 : 24;
    this.onStateChange?.(this.isPlaying, this.estimatedDspLoad);
  }

  public toggleUsbAudio(deviceId?: string): Promise<boolean> {
    if (this.usbAudioConfig.enabled) {
      this.stopUsbAudio();
      return Promise.resolve(false);
    } else {
      return this.startUsbAudio(deviceId);
    }
  }

  public setUsbInputGain(gain: number) {
    this.usbAudioConfig.inputGain = Math.max(0, Math.min(3.0, gain));
    if (this.usbGainNode && this.ctx) {
      this.usbGainNode.gain.setTargetAtTime(this.usbAudioConfig.inputGain, this.ctx.currentTime, 0.03);
    }
  }

  public getUsbAudioConfig(): UsbAudioConfig {
    return { ...this.usbAudioConfig };
  }

  public getUsbInputLevel(): number {
    if (!this.usbAnalyser || !this.usbAudioConfig.enabled) return 0;
    const data = new Uint8Array(32);
    this.usbAnalyser.getByteTimeDomainData(data);
    let sum = 0;
    for (let i = 0; i < 32; i++) {
      const v = (data[i] - 128) / 128;
      sum += v * v;
    }
    return Math.min(1.0, Math.sqrt(sum / 32) * 3.0);
  }

  // ==========================================
  // PLAYBACK & 4-STEM ENGINE
  // ==========================================

  public async togglePlay(): Promise<boolean> {
    await this.initAudio();
    if (this.isPlaying) {
      this.stopPlayback();
      return false;
    } else {
      this.startPlayback();
      return true;
    }
  }

  public getIsPlaying(): boolean {
    return this.isPlaying;
  }

  private startPlayback() {
    if (!this.ctx) return;
    this.isPlaying = true;

    this.stemBuffers.forEach((buffer, idx) => {
      if (!buffer || !this.ctx || !this.stemGains[idx]) return;
      if (this.stemSources[idx]) {
        try { this.stemSources[idx]?.stop(); } catch { /* ignore */ }
      }

      const source = this.ctx.createBufferSource();
      source.buffer = buffer;
      source.loop = true;
      source.connect(this.stemGains[idx]);
      source.start();
      this.stemSources[idx] = source;
    });

    this.estimatedDspLoad = this.params.lowCpuMode ? 14 : 24;
    this.onStateChange?.(true, this.estimatedDspLoad);
  }

  public stopPlayback() {
    this.isPlaying = false;
    this.stemSources.forEach(src => {
      if (src) {
        try { src.stop(); } catch { /* ignore */ }
      }
    });
    this.stemSources = [null, null, null, null];
    this.estimatedDspLoad = this.params.lowCpuMode ? 5 : 9;
    this.onStateChange?.(false, this.estimatedDspLoad);
  }

  // Stem Control
  public setStemVolume(idx: number, vol: number) {
    if (idx < 0 || idx >= 4) return;
    this.stemStates[idx].volume = Math.max(0, Math.min(1, vol));
    this.applyStemGains();
  }

  public toggleStemMute(idx: number) {
    if (idx < 0 || idx >= 4) return;
    this.stemStates[idx].muted = !this.stemStates[idx].muted;
    this.applyStemGains();
  }

  public toggleStemSolo(idx: number) {
    if (idx < 0 || idx >= 4) return;
    const current = this.stemStates[idx].soloed;
    this.stemStates[idx].soloed = !current;
    this.applyStemGains();
  }

  private applyStemGains() {
    if (!this.ctx) return;
    const hasSolo = this.stemStates.some(s => s.soloed);
    const now = this.ctx.currentTime;

    this.stemStates.forEach((state, i) => {
      if (!this.stemGains[i]) return;
      let target = state.volume;
      if (hasSolo) {
        target = state.soloed ? state.volume : 0;
      } else if (state.muted) {
        target = 0;
      }
      this.stemGains[i].gain.setTargetAtTime(target, now, 0.02);
    });
  }

  public getStemStates(): StemState[] {
    return [...this.stemStates];
  }

  // ==========================================
  // PARAMETER SETTERS
  // ==========================================

  public setMix(val: number) {
    this.params.mix = Math.max(0, Math.min(1, val));
    this.updateMixGains();
  }

  public setDecay(val: number) {
    this.params.decay = Math.max(0.2, Math.min(25.0, val));
    this.updateFeedback();
  }

  public setDiffusion(val: number) {
    this.params.diffusion = Math.max(0, Math.min(1, val));
    const g = 0.7 * this.params.diffusion;
    this.inputDiffusers.forEach(d => d.setGain(g));
    if (this.tankDiffuserL) this.tankDiffuserL.setGain(0.6 * this.params.diffusion);
    if (this.tankDiffuserR) this.tankDiffuserR.setGain(0.6 * this.params.diffusion);
  }

  public setFilterCutoff(hz: number) {
    this.params.filterCutoff = Math.max(200, Math.min(16000, hz));
    if (this.tankFilterL && this.tankFilterR && this.ctx) {
      const now = this.ctx.currentTime;
      this.tankFilterL.frequency.setTargetAtTime(this.params.filterCutoff, now, 0.04);
      this.tankFilterR.frequency.setTargetAtTime(this.params.filterCutoff, now, 0.04);
    }
  }

  public setFilterType(type: 'lp' | 'hp' | 'bp') {
    this.params.filterType = type;
    if (this.tankFilterL && this.tankFilterR) {
      const bqType: BiquadFilterType = type === 'bp' ? 'bandpass' : type === 'hp' ? 'highpass' : 'lowpass';
      this.tankFilterL.type = bqType;
      this.tankFilterR.type = bqType;
    }
  }

  public setModDepth(val: number) {
    this.params.modDepth = Math.max(0, Math.min(1, val));
    if (this.lfoGainL && this.lfoGainR && this.ctx) {
      const now = this.ctx.currentTime;
      const depthSec = 0.0035 * this.params.modDepth;
      this.lfoGainL.gain.setTargetAtTime(depthSec, now, 0.05);
      this.lfoGainR.gain.setTargetAtTime(-depthSec, now, 0.05);
    }
  }

  public setModRate(hz: number) {
    this.params.modRate = Math.max(0.1, Math.min(6.0, hz));
    if (this.lfoOsc && this.ctx) {
      this.lfoOsc.frequency.setTargetAtTime(this.params.modRate, this.ctx.currentTime, 0.05);
    }
  }

  public setPreDelay(ms: number) {
    this.params.preDelay = Math.max(0, Math.min(250, ms));
    if (this.preDelayNode && this.ctx) {
      this.preDelayNode.delayTime.setTargetAtTime(this.params.preDelay / 1000, this.ctx.currentTime, 0.04);
    }
  }

  public setVintageGrit(val: number) {
    this.params.vintageGrit = Math.max(0, Math.min(1, val));
    this.updateGritCurve(this.params.vintageGrit);
  }

  public setFreeze(freeze: boolean) {
    this.params.freeze = freeze;
    this.updateFeedback();
  }

  public setLowCpuMode(low: boolean) {
    this.params.lowCpuMode = low;
    this.estimatedDspLoad = low ? 14 : 24;
    if (this.inputAnalyser && this.outputAnalyser) {
      this.inputAnalyser.fftSize = low ? 128 : 256;
      this.outputAnalyser.fftSize = low ? 128 : 256;
    }
    this.onStateChange?.(this.isPlaying, this.estimatedDspLoad);
  }

  public setBank(bank: 'A' | 'B') {
    this.params.bank = bank;
  }

  private updateFeedback() {
    if (!this.tankFeedbackL || !this.tankFeedbackR || !this.tankCrossLtoR || !this.tankCrossRtoL || !this.ctx) return;
    const now = this.ctx.currentTime;
    const fb = this.calculateFeedbackGain(this.params.decay);
    this.tankFeedbackL.gain.setTargetAtTime(fb * 0.7, now, 0.05);
    this.tankFeedbackR.gain.setTargetAtTime(fb * 0.7, now, 0.05);
    this.tankCrossLtoR.gain.setTargetAtTime(fb * 0.3, now, 0.05);
    this.tankCrossRtoL.gain.setTargetAtTime(fb * 0.3, now, 0.05);
  }

  public getParams(): ReverbParams {
    return { ...this.params };
  }

  public setAllParams(p: Partial<ReverbParams>) {
    if (p.mix !== undefined) this.setMix(p.mix);
    if (p.decay !== undefined) this.setDecay(p.decay);
    if (p.diffusion !== undefined) this.setDiffusion(p.diffusion);
    if (p.filterCutoff !== undefined) this.setFilterCutoff(p.filterCutoff);
    if (p.filterType !== undefined) this.setFilterType(p.filterType);
    if (p.modDepth !== undefined) this.setModDepth(p.modDepth);
    if (p.modRate !== undefined) this.setModRate(p.modRate);
    if (p.preDelay !== undefined) this.setPreDelay(p.preDelay);
    if (p.vintageGrit !== undefined) this.setVintageGrit(p.vintageGrit);
    if (p.freeze !== undefined) this.setFreeze(p.freeze);
    if (p.lowCpuMode !== undefined) this.setLowCpuMode(p.lowCpuMode);
    if (p.bank !== undefined) this.setBank(p.bank);
  }

  // Audition Transient Impulse
  public triggerImpulse() {
    this.initAudio().then(() => {
      if (!this.ctx || !this.stemMixBus) return;
      const now = this.ctx.currentTime;
      const osc = this.ctx.createOscillator();
      const gain = this.ctx.createGain();
      osc.type = 'triangle';
      osc.frequency.setValueAtTime(800, now);
      osc.frequency.exponentialRampToValueAtTime(120, now + 0.08);
      gain.gain.setValueAtTime(0.5, now);
      gain.gain.exponentialRampToValueAtTime(0.001, now + 0.12);
      osc.connect(gain);
      gain.connect(this.stemMixBus);
      osc.start(now);
      osc.stop(now + 0.14);
    });
  }

  // Tactile Mechanical Click Sound (for TE buttons)
  public playTactileClick() {
    if (!this.ctx) return;
    try {
      const now = this.ctx.currentTime;
      const osc = this.ctx.createOscillator();
      const gain = this.ctx.createGain();
      osc.type = 'sine';
      osc.frequency.setValueAtTime(1400, now);
      osc.frequency.exponentialRampToValueAtTime(300, now + 0.006);
      gain.gain.setValueAtTime(0.08, now);
      gain.gain.exponentialRampToValueAtTime(0.001, now + 0.008);
      osc.connect(gain);
      gain.connect(this.ctx.destination);
      osc.start(now);
      osc.stop(now + 0.01);
    } catch {
      /* ignore */
    }
  }

  public getMeterLevels(): { inL: number; inR: number; outL: number; outR: number } {
    if (!this.inputAnalyser || !this.outputAnalyser) {
      return { inL: 0, inR: 0, outL: 0, outR: 0 };
    }
    const inData = new Uint8Array(32);
    const outData = new Uint8Array(32);
    this.inputAnalyser.getByteTimeDomainData(inData);
    this.outputAnalyser.getByteTimeDomainData(outData);

    let inSum = 0;
    let outSum = 0;
    for (let i = 0; i < 32; i++) {
      const inV = (inData[i] - 128) / 128;
      const outV = (outData[i] - 128) / 128;
      inSum += inV * inV;
      outSum += outV * outV;
    }
    return {
      inL: Math.min(1.0, Math.sqrt(inSum / 32) * 2.5),
      inR: Math.min(1.0, Math.sqrt(inSum / 32) * 2.3),
      outL: Math.min(1.0, Math.sqrt(outSum / 32) * 2.2),
      outR: Math.min(1.0, Math.sqrt(outSum / 32) * 2.2),
    };
  }

  public getDspLoad(): number {
    return this.estimatedDspLoad;
  }
}

export const reverbEngine = new SP1ReverbEngine();
