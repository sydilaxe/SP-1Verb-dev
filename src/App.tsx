import React, { useState, useEffect, useCallback, useRef } from 'react';
import { reverbEngine, ReverbParams, DEFAULT_PARAMS, StemState, UsbAudioConfig } from './audio/reverbEngine';
import { PRESETS, ReverbPreset } from './audio/presets';
import { TouchLightStrip } from './components/TouchLightStrip';
import { 
  Play, 
  Square, 
  Cable, 
  Lock, 
  Sparkles, 
  Info, 
  ChevronLeft, 
  ChevronRight,
  Layers,
  Usb,
  Cpu
} from 'lucide-react';

export default function App() {
  const [params, setParams] = useState<ReverbParams>({ ...DEFAULT_PARAMS });
  const [activePreset, setActivePreset] = useState<ReverbPreset>(PRESETS[0]);
  const [isPlaying, setIsPlaying] = useState(false);
  const [dspLoad, setDspLoad] = useState(14);
  const [activeBank, setActiveBank] = useState<'A' | 'B'>('A');
  const [stemsMode, setStemsMode] = useState(false);
  const [stemStates, setStemStates] = useState<StemState[]>([
    { volume: 0.85, muted: false, soloed: false },
    { volume: 0.80, muted: false, soloed: false },
    { volume: 0.75, muted: false, soloed: false },
    { volume: 0.80, muted: false, soloed: false },
  ]);
  const [autoSwitchOnPlay, setAutoSwitchOnPlay] = useState(true);

  // Screenless Preset Indicator: flashes LED pattern on the 4 light strips
  const [presetFlashNumber, setPresetFlashNumber] = useState<number | null>(null);
  const flashTimeoutRef = useRef<number | null>(null);

  // USB Audio & Web Serial (solderless.engineering / timknapen/SP-1-dev)
  const [usbConfig, setUsbConfig] = useState<UsbAudioConfig>(reverbEngine.getUsbAudioConfig());
  const [showUsbModal, setShowUsbModal] = useState(false);
  const [availableUsbDevices, setAvailableUsbDevices] = useState<MediaDeviceInfo[]>([]);
  const [usbLevel, setUsbLevel] = useState(0);
  const [serialConnected, setSerialConnected] = useState(false);
  const [serialStatus, setSerialStatus] = useState<string>('Not connected');

  // Architecture Spec Modal (Off-device documentation)
  const [showDocModal, setShowDocModal] = useState(false);

  // Initialize engine listener
  useEffect(() => {
    reverbEngine.setListener((playing, load) => {
      setIsPlaying(playing);
      setDspLoad(load);
    });

    let animId: number;
    const pollUsb = () => {
      animId = requestAnimationFrame(pollUsb);
      if (reverbEngine.getUsbAudioConfig().enabled) {
        setUsbLevel(reverbEngine.getUsbInputLevel());
      } else {
        setUsbLevel(0);
      }
    };
    pollUsb();
    return () => cancelAnimationFrame(animId);
  }, []);

  // Update Reverb parameter
  const updateParam = useCallback(<K extends keyof ReverbParams>(key: K, val: ReverbParams[K]) => {
    setParams(prev => {
      const updated = { ...prev, [key]: val };
      reverbEngine.setAllParams({ [key]: val });
      return updated;
    });
  }, []);

  // Central TE Play Button Handler
  // CRITICAL REQUIREMENT: Play button switches to second set of reverb controls
  // for real-time modulation of tone character!
  const handleTogglePlay = async () => {
    reverbEngine.playTactileClick();
    const willPlay = await reverbEngine.togglePlay();
    setIsPlaying(willPlay);

    if (autoSwitchOnPlay && !stemsMode) {
      if (willPlay) {
        setActiveBank('B');
        updateParam('bank', 'B');
      } else {
        setActiveBank('A');
        updateParam('bank', 'A');
      }
    }
  };

  // Switch Bank (A = Core Reverb, B = Real-time Tone Modulation)
  const handleBankSwitch = (bank: 'A' | 'B') => {
    reverbEngine.playTactileClick();
    setActiveBank(bank);
    setStemsMode(false);
    updateParam('bank', bank);
  };

  // Toggle Stems Control Mode
  const handleToggleStemsMode = () => {
    reverbEngine.playTactileClick();
    setStemsMode(prev => !prev);
  };

  // Stem Volume / Mute / Solo
  const handleStemVolumeChange = (idx: number, vol: number) => {
    reverbEngine.setStemVolume(idx, vol);
    setStemStates(reverbEngine.getStemStates());
  };

  const handleToggleStemMute = (idx: number, e?: React.MouseEvent) => {
    e?.stopPropagation();
    reverbEngine.playTactileClick();
    reverbEngine.toggleStemMute(idx);
    setStemStates(reverbEngine.getStemStates());
  };

  const handleToggleStemSolo = (idx: number, e?: React.MouseEvent) => {
    e?.stopPropagation();
    reverbEngine.playTactileClick();
    reverbEngine.toggleStemSolo(idx);
    setStemStates(reverbEngine.getStemStates());
  };

  // Preset Selection with Screenless LED feedback
  const applyPreset = (preset: ReverbPreset) => {
    reverbEngine.playTactileClick();
    setActivePreset(preset);
    const updated = { ...params, ...preset.params };
    setParams(updated);
    reverbEngine.setAllParams(updated);

    // Screenless hardware visual feedback: pulse the preset number (1-8)
    const idx = PRESETS.findIndex(p => p.id === preset.id) + 1;
    setPresetFlashNumber(idx);
    if (flashTimeoutRef.current) clearTimeout(flashTimeoutRef.current);
    flashTimeoutRef.current = window.setTimeout(() => {
      setPresetFlashNumber(null);
    }, 1200);
  };

  // Next / Previous Preset Stepper (Hardware Rocker Switch)
  const stepPreset = (direction: 'next' | 'prev') => {
    const currentIndex = PRESETS.findIndex(p => p.id === activePreset.id);
    let nextIndex = direction === 'next' ? currentIndex + 1 : currentIndex - 1;
    if (nextIndex >= PRESETS.length) nextIndex = 0;
    if (nextIndex < 0) nextIndex = PRESETS.length - 1;
    applyPreset(PRESETS[nextIndex]);
  };

  // Keyboard Shortcuts for Screenless Hardware Operation
  useEffect(() => {
    const handleKeyDown = (e: KeyboardEvent) => {
      if (['INPUT', 'TEXTAREA'].includes((e.target as HTMLElement)?.tagName)) return;

      if (e.key === 'ArrowRight' || e.key === ']') {
        e.preventDefault();
        stepPreset('next');
      } else if (e.key === 'ArrowLeft' || e.key === '[') {
        e.preventDefault();
        stepPreset('prev');
      } else if (e.key >= '1' && e.key <= '8' && !e.ctrlKey && !e.metaKey && !e.altKey) {
        const idx = parseInt(e.key, 10) - 1;
        if (PRESETS[idx]) {
          e.preventDefault();
          applyPreset(PRESETS[idx]);
        }
      } else if (e.code === 'Space') {
        e.preventDefault();
        handleTogglePlay();
      }
    };

    window.addEventListener('keydown', handleKeyDown);
    return () => window.removeEventListener('keydown', handleKeyDown);
  }, [activePreset, params, autoSwitchOnPlay, stemsMode]);

  // USB Audio Ingest
  const handleToggleUsbAudio = async (deviceId?: string) => {
    reverbEngine.playTactileClick();
    const enabled = await reverbEngine.toggleUsbAudio(deviceId);
    setUsbConfig(reverbEngine.getUsbAudioConfig());
    const devs = await reverbEngine.getAvailableAudioDevices();
    setAvailableUsbDevices(devs);
  };

  // Web Serial Connection (for SP-1 hardware over USB via solderless.engineering / nRF52840)
  const handleConnectSerial = async () => {
    if (!('serial' in navigator)) {
      alert('Web Serial is supported in Chrome, Edge, and Opera. Connect your SP-1 via USB-C.');
      return;
    }
    try {
      const navSerial = (navigator as unknown as { serial: { requestPort: () => Promise<unknown> } }).serial;
      const port = await navSerial.requestPort();
      if (port) {
        setSerialConnected(true);
        setSerialStatus('Connected to SP-1 (nRF52840)');
        reverbEngine.playTactileClick();
      }
    } catch {
      setSerialStatus('Serial connection cancelled');
    }
  };

  const currentPresetIndex = PRESETS.findIndex(p => p.id === activePreset.id) + 1;

  return (
    <div className="min-h-screen bg-[#0d0d0f] text-[#ede9e1] flex flex-col font-sans select-none antialiased justify-between">
      
      {/* Top Header: Teenage Engineering SP-1 Stem Player Brand & Link */}
      <header className="border-b border-zinc-800/80 bg-zinc-950/80 backdrop-blur px-4 sm:px-8 py-3 flex items-center justify-between">
        <div className="flex items-center gap-2 sm:gap-3">
          <span className="w-3 h-3 rounded-full bg-[#ff5722] shadow-[0_0_8px_#ff5722]" />
          <span className="font-mono text-xs font-bold tracking-widest text-[#ede9e1] uppercase">
            TEENAGE ENGINEERING
          </span>
          <span className="text-zinc-600 font-mono text-xs">/</span>
          <span className="font-mono text-xs font-bold tracking-widest text-[#ff5722] uppercase">
            SP-1 STEM PLAYER
          </span>
          <span className="text-[10px] font-mono px-1.5 py-0.5 rounded bg-zinc-800 text-zinc-400 border border-zinc-700 hidden md:inline">
            nRF52840 64MHz DSP · SCREENLESS
          </span>
        </div>

        {/* Global Hardware Utilities & USB-C Link */}
        <div className="flex items-center gap-2">
          {/* Web Serial / Hardware Link */}
          <button
            onClick={handleConnectSerial}
            className={`flex items-center gap-1.5 px-2.5 py-1 rounded text-xs font-mono font-bold border transition-all ${
              serialConnected
                ? 'bg-emerald-500/20 border-emerald-500 text-emerald-300'
                : 'bg-zinc-900 border-zinc-700 text-zinc-400 hover:text-zinc-200'
            }`}
            title="Connect SP-1 hardware via Web Serial (solderless.engineering protocol)"
          >
            <Usb className="w-3 h-3" />
            <span>{serialConnected ? 'SP-1 LINKED' : 'LINK USB'}</span>
          </button>

          {/* Live USB Audio DSP Input */}
          <button
            onClick={() => {
              if (!usbConfig.enabled) handleToggleUsbAudio();
              else setShowUsbModal(true);
            }}
            className={`flex items-center gap-1.5 px-2.5 py-1 rounded text-xs font-mono font-bold border transition-all ${
              usbConfig.enabled
                ? 'bg-cyan-500/20 border-cyan-500 text-cyan-300 shadow-[0_0_10px_rgba(6,182,212,0.4)]'
                : 'bg-zinc-900 border-zinc-700 text-zinc-400 hover:text-zinc-200'
            }`}
            title="Live USB-C audio in processing"
          >
            <Cable className={`w-3 h-3 ${usbConfig.enabled ? 'text-cyan-400 animate-pulse' : 'text-zinc-500'}`} />
            <span>{usbConfig.enabled ? 'USB AUDIO: ON' : 'USB AUDIO'}</span>
          </button>

          {/* Reference Spec */}
          <button
            onClick={() => setShowDocModal(true)}
            className="flex items-center gap-1 px-2.5 py-1 rounded text-xs font-mono text-zinc-400 hover:text-zinc-200 bg-zinc-900 border border-zinc-800 transition-colors"
            title="SP-1 Hardware Architecture Reference"
          >
            <Info className="w-3 h-3" />
            <span className="hidden sm:inline">Spec</span>
          </button>
        </div>
      </header>

      {/* MAIN SCREENLESS HARDWARE: The Teenage Engineering SP-1 Stem Player */}
      <main className="flex-1 w-full max-w-4xl mx-auto p-4 sm:p-8 flex flex-col justify-center items-center">
        
        {/* PHYSICAL DEVICE PUCK: Soft-Touch Matte Pebble Casing (Zero Screen, 100% Tactile) */}
        <div className="relative rounded-[48px] sm:rounded-[64px] bg-gradient-to-b from-[#2d2a27] via-[#211f1e] to-[#141313] border-2 border-[#423e39] shadow-[0_35px_90px_rgba(0,0,0,0.95),inset_0_2px_4px_rgba(255,255,255,0.08)] p-6 sm:p-10 flex flex-col items-center gap-6 sm:gap-8 w-full max-w-2xl">
          
          {/* Soft-touch silicone radial light gradient */}
          <div className="absolute inset-0 rounded-[46px] sm:rounded-[62px] bg-[radial-gradient(ellipse_at_center,_transparent_45%,_rgba(0,0,0,0.65)_100%)] pointer-events-none" />

          {/* TOP PERIMETER: Physical Rocker (< / >) & Status LEDs (NO SCREEN) */}
          <div className="relative z-10 w-full flex items-center justify-between px-2 pt-1">
            
            {/* PHYSICAL PRESET ROCKER SWITCH (< / >) */}
            <div className="flex items-center gap-1.5 p-1 rounded-full bg-zinc-950/80 border border-zinc-700/80 shadow-inner">
              <button
                onClick={() => stepPreset('prev')}
                className="w-8 h-8 rounded-full bg-zinc-800 hover:bg-zinc-700 active:scale-90 flex items-center justify-center text-zinc-300 font-bold border border-zinc-700 transition-transform"
                title="Previous Preset (or press Left Arrow / [ )"
              >
                <ChevronLeft className="w-4 h-4" />
              </button>

              <div className="px-2 flex flex-col items-center">
                <span className="text-[8px] font-mono font-bold text-zinc-500 uppercase tracking-widest">
                  ALGO ROCKER
                </span>
                <span className="text-xs font-mono font-bold text-[#ff5722]">
                  #{currentPresetIndex} {activePreset.name.split(' ')[0]}
                </span>
              </div>

              <button
                onClick={() => stepPreset('next')}
                className="w-8 h-8 rounded-full bg-zinc-800 hover:bg-zinc-700 active:scale-90 flex items-center justify-center text-zinc-300 font-bold border border-zinc-700 transition-transform"
                title="Next Preset (or press Right Arrow / ] )"
              >
                <ChevronRight className="w-4 h-4" />
              </button>
            </div>

            {/* SCREENLESS HARDWARE STATUS LEDS */}
            <div className="flex items-center gap-3">
              {/* Active Layer LED Indicator */}
              <div className="flex items-center gap-1.5 px-3 py-1.5 rounded-full bg-zinc-950/80 border border-zinc-800 font-mono text-[10px]">
                <span
                  className={`w-2 h-2 rounded-full ${
                    stemsMode
                      ? 'bg-amber-400 shadow-[0_0_8px_#f59e0b]'
                      : activeBank === 'B'
                      ? 'bg-[#00f0ff] shadow-[0_0_8px_#00f0ff]'
                      : 'bg-[#ff5722] shadow-[0_0_8px_#ff5722]'
                  }`}
                />
                <span className="font-bold tracking-wider text-zinc-300 uppercase">
                  {stemsMode ? 'STEMS' : activeBank === 'A' ? 'CORE REVERB' : 'TONE MOD'}
                </span>
              </div>

              {/* Hardware Layer Toggle Button */}
              <button
                onClick={() => handleBankSwitch(activeBank === 'A' ? 'B' : 'A')}
                className={`p-2 rounded-full border transition-all ${
                  activeBank === 'B'
                    ? 'bg-[#00f0ff]/20 border-[#00f0ff] text-[#00f0ff] shadow-[0_0_8px_rgba(0,240,255,0.4)]'
                    : 'bg-zinc-900 border-zinc-700 text-zinc-400 hover:text-zinc-200'
                }`}
                title="Toggle between Set 1 (Core) and Set 2 (Tone Mod)"
              >
                <Layers className="w-4 h-4" />
              </button>
            </div>
          </div>

          {/* SCREENLESS PRESET LIGHT FLASH (LED feedback across the surface on preset change) */}
          {presetFlashNumber !== null && (
            <div className="relative z-20 w-full py-1.5 px-4 rounded-xl bg-[#ff5722]/15 border border-[#ff5722]/60 text-[#ff5722] font-mono text-xs flex items-center justify-between">
              <span className="font-bold tracking-wider uppercase">
                ALGORITHM #{presetFlashNumber}: {activePreset.name} ({activePreset.year})
              </span>
              <span className="text-[10px] text-[#ff7a50]">{activePreset.description}</span>
            </div>
          )}

          {/* THE FOUR ILLUMINATED LIGHT STRIPS (The Stem Player's Famous Touch Sliders) */}
          <div className="relative z-10 w-full rounded-3xl bg-zinc-950/90 border border-zinc-800/80 p-5 sm:p-7 shadow-inner flex flex-col items-center">
            
            {/* The 4 Light Strips in a Row */}
            <div className="grid grid-cols-4 gap-3 sm:gap-8 w-full justify-items-center">
              
              {/* STRIP 1: Reverb Mix (Set 1) OR Mod Depth (Set 2) OR Stem 1 Drums */}
              {stemsMode ? (
                <div className="flex flex-col items-center">
                  <TouchLightStrip
                    id="strip-stem-1"
                    indexNumber={1}
                    label="Drums"
                    sublabel="Stem 1"
                    value={stemStates[0].volume}
                    min={0.0}
                    max={1.0}
                    step={0.01}
                    unit="%"
                    formatValue={v => `${(v * 100).toFixed(0)}`}
                    colorScheme="orange"
                    onChange={v => handleStemVolumeChange(0, v)}
                    lowCpuMode={params.lowCpuMode}
                  />
                  <div className="mt-2 flex items-center gap-1 font-mono text-[9px]">
                    <button
                      onClick={e => handleToggleStemMute(0, e)}
                      className={`px-1.5 py-0.5 rounded ${
                        stemStates[0].muted ? 'bg-red-500 text-white font-bold' : 'bg-zinc-800 text-zinc-400'
                      }`}
                    >
                      M
                    </button>
                    <button
                      onClick={e => handleToggleStemSolo(0, e)}
                      className={`px-1.5 py-0.5 rounded ${
                        stemStates[0].soloed ? 'bg-amber-400 text-zinc-950 font-bold' : 'bg-zinc-800 text-zinc-400'
                      }`}
                    >
                      S
                    </button>
                  </div>
                </div>
              ) : activeBank === 'A' ? (
                <TouchLightStrip
                  id="strip-mix"
                  indexNumber={1}
                  label="Reverb Mix"
                  sublabel="Dry / Wet"
                  value={params.mix}
                  min={0.0}
                  max={1.0}
                  step={0.01}
                  unit="%"
                  formatValue={v => `${(v * 100).toFixed(0)}`}
                  colorScheme="white"
                  onChange={v => updateParam('mix', v)}
                  lowCpuMode={params.lowCpuMode}
                />
              ) : (
                <TouchLightStrip
                  id="strip-mod-depth"
                  indexNumber={1}
                  label="Mod Depth"
                  sublabel="Wander"
                  value={params.modDepth}
                  min={0.0}
                  max={1.0}
                  step={0.01}
                  unit="%"
                  formatValue={v => `${(v * 100).toFixed(0)}`}
                  colorScheme="cyan"
                  onChange={v => updateParam('modDepth', v)}
                  lowCpuMode={params.lowCpuMode}
                />
              )}

              {/* STRIP 2: Decay (Set 1) OR Mod Rate (Set 2) OR Stem 2 Bass */}
              {stemsMode ? (
                <div className="flex flex-col items-center">
                  <TouchLightStrip
                    id="strip-stem-2"
                    indexNumber={2}
                    label="Bass"
                    sublabel="Stem 2"
                    value={stemStates[1].volume}
                    min={0.0}
                    max={1.0}
                    step={0.01}
                    unit="%"
                    formatValue={v => `${(v * 100).toFixed(0)}`}
                    colorScheme="orange"
                    onChange={v => handleStemVolumeChange(1, v)}
                    lowCpuMode={params.lowCpuMode}
                  />
                  <div className="mt-2 flex items-center gap-1 font-mono text-[9px]">
                    <button
                      onClick={e => handleToggleStemMute(1, e)}
                      className={`px-1.5 py-0.5 rounded ${
                        stemStates[1].muted ? 'bg-red-500 text-white font-bold' : 'bg-zinc-800 text-zinc-400'
                      }`}
                    >
                      M
                    </button>
                    <button
                      onClick={e => handleToggleStemSolo(1, e)}
                      className={`px-1.5 py-0.5 rounded ${
                        stemStates[1].soloed ? 'bg-amber-400 text-zinc-950 font-bold' : 'bg-zinc-800 text-zinc-400'
                      }`}
                    >
                      S
                    </button>
                  </div>
                </div>
              ) : activeBank === 'A' ? (
                <TouchLightStrip
                  id="strip-decay"
                  indexNumber={2}
                  label="Decay"
                  sublabel="RT60"
                  value={params.decay}
                  min={0.2}
                  max={25.0}
                  step={0.1}
                  unit="s"
                  formatValue={v => v.toFixed(1)}
                  colorScheme="white"
                  onChange={v => updateParam('decay', v)}
                  lowCpuMode={params.lowCpuMode}
                />
              ) : (
                <TouchLightStrip
                  id="strip-mod-rate"
                  indexNumber={2}
                  label="Mod Rate"
                  sublabel="Spin"
                  value={params.modRate}
                  min={0.1}
                  max={6.0}
                  step={0.05}
                  unit="Hz"
                  formatValue={v => v.toFixed(2)}
                  colorScheme="cyan"
                  onChange={v => updateParam('modRate', v)}
                  lowCpuMode={params.lowCpuMode}
                />
              )}

              {/* STRIP 3: Diffusion (Set 1) OR Pre-Delay (Set 2) OR Stem 3 Synth */}
              {stemsMode ? (
                <div className="flex flex-col items-center">
                  <TouchLightStrip
                    id="strip-stem-3"
                    indexNumber={3}
                    label="Synths"
                    sublabel="Stem 3"
                    value={stemStates[2].volume}
                    min={0.0}
                    max={1.0}
                    step={0.01}
                    unit="%"
                    formatValue={v => `${(v * 100).toFixed(0)}`}
                    colorScheme="orange"
                    onChange={v => handleStemVolumeChange(2, v)}
                    lowCpuMode={params.lowCpuMode}
                  />
                  <div className="mt-2 flex items-center gap-1 font-mono text-[9px]">
                    <button
                      onClick={e => handleToggleStemMute(2, e)}
                      className={`px-1.5 py-0.5 rounded ${
                        stemStates[2].muted ? 'bg-red-500 text-white font-bold' : 'bg-zinc-800 text-zinc-400'
                      }`}
                    >
                      M
                    </button>
                    <button
                      onClick={e => handleToggleStemSolo(2, e)}
                      className={`px-1.5 py-0.5 rounded ${
                        stemStates[2].soloed ? 'bg-amber-400 text-zinc-950 font-bold' : 'bg-zinc-800 text-zinc-400'
                      }`}
                    >
                      S
                    </button>
                  </div>
                </div>
              ) : activeBank === 'A' ? (
                <TouchLightStrip
                  id="strip-diff"
                  indexNumber={3}
                  label="Diffusion"
                  sublabel="Smear"
                  value={params.diffusion}
                  min={0.0}
                  max={1.0}
                  step={0.01}
                  unit="%"
                  formatValue={v => `${(v * 100).toFixed(0)}`}
                  colorScheme="white"
                  onChange={v => updateParam('diffusion', v)}
                  lowCpuMode={params.lowCpuMode}
                />
              ) : (
                <TouchLightStrip
                  id="strip-predelay"
                  indexNumber={3}
                  label="Pre-Delay"
                  sublabel="Onset Gap"
                  value={params.preDelay}
                  min={0}
                  max={250}
                  step={1}
                  unit="ms"
                  formatValue={v => v.toFixed(0)}
                  colorScheme="cyan"
                  onChange={v => updateParam('preDelay', v)}
                  lowCpuMode={params.lowCpuMode}
                />
              )}

              {/* STRIP 4: Filter (Set 1) OR Vintage Grit (Set 2) OR Stem 4 Vocal */}
              {stemsMode ? (
                <div className="flex flex-col items-center">
                  <TouchLightStrip
                    id="strip-stem-4"
                    indexNumber={4}
                    label="Vocals"
                    sublabel="Stem 4"
                    value={stemStates[3].volume}
                    min={0.0}
                    max={1.0}
                    step={0.01}
                    unit="%"
                    formatValue={v => `${(v * 100).toFixed(0)}`}
                    colorScheme="orange"
                    onChange={v => handleStemVolumeChange(3, v)}
                    lowCpuMode={params.lowCpuMode}
                  />
                  <div className="mt-2 flex items-center gap-1 font-mono text-[9px]">
                    <button
                      onClick={e => handleToggleStemMute(3, e)}
                      className={`px-1.5 py-0.5 rounded ${
                        stemStates[3].muted ? 'bg-red-500 text-white font-bold' : 'bg-zinc-800 text-zinc-400'
                      }`}
                    >
                      M
                    </button>
                    <button
                      onClick={e => handleToggleStemSolo(3, e)}
                      className={`px-1.5 py-0.5 rounded ${
                        stemStates[3].soloed ? 'bg-amber-400 text-zinc-950 font-bold' : 'bg-zinc-800 text-zinc-400'
                      }`}
                    >
                      S
                    </button>
                  </div>
                </div>
              ) : activeBank === 'A' ? (
                <div className="flex flex-col items-center">
                  <TouchLightStrip
                    id="strip-filter"
                    indexNumber={4}
                    label="Filter"
                    sublabel="HP / LP"
                    value={params.filterCutoff}
                    min={200}
                    max={16000}
                    step={50}
                    unit="Hz"
                    formatValue={v => (v >= 1000 ? `${(v / 1000).toFixed(1)}k` : `${v.toFixed(0)}`)}
                    colorScheme="white"
                    onChange={v => updateParam('filterCutoff', v)}
                    lowCpuMode={params.lowCpuMode}
                  />
                  {/* Filter Mode LP / BP / HP buttons */}
                  <div className="mt-2 flex items-center gap-1 p-0.5 rounded bg-zinc-900 border border-zinc-800 font-mono text-[9px]">
                    {(['lp', 'bp', 'hp'] as const).map(mode => (
                      <button
                        key={mode}
                        onClick={() => {
                          reverbEngine.playTactileClick();
                          updateParam('filterType', mode);
                        }}
                        className={`px-1.5 py-0.5 rounded uppercase font-bold transition-colors ${
                          params.filterType === mode
                            ? 'bg-[#ff5722] text-zinc-950 shadow-sm'
                            : 'text-zinc-500 hover:text-zinc-300'
                        }`}
                      >
                        {mode}
                      </button>
                    ))}
                  </div>
                </div>
              ) : (
                <TouchLightStrip
                  id="strip-grit"
                  indexNumber={4}
                  label="12-Bit Grit"
                  sublabel="DAC Dirt"
                  value={params.vintageGrit}
                  min={0.0}
                  max={1.0}
                  step={0.01}
                  unit="%"
                  formatValue={v => `${(v * 100).toFixed(0)}`}
                  colorScheme="cyan"
                  onChange={v => updateParam('vintageGrit', v)}
                  lowCpuMode={params.lowCpuMode}
                />
              )}

            </div>
          </div>

          {/* CENTER SILICONE PLAY BUTTON & TACTILE CONTROLS */}
          <div className="relative z-10 w-full flex items-center justify-between px-2 pt-2">
            
            {/* Left Controls: Freeze & Impulse Hit */}
            <div className="flex items-center gap-2">
              <button
                onClick={() => {
                  reverbEngine.playTactileClick();
                  updateParam('freeze', !params.freeze);
                }}
                className={`w-11 h-11 rounded-full border flex items-center justify-center transition-all ${
                  params.freeze
                    ? 'bg-red-500/25 border-red-500 text-red-400 shadow-[0_0_12px_rgba(239,68,68,0.5)] animate-pulse'
                    : 'bg-zinc-900 border-zinc-700 text-zinc-400 hover:text-zinc-200'
                }`}
                title="Freeze reverb tail"
              >
                <Lock className="w-4 h-4" />
              </button>

              <button
                onClick={() => {
                  reverbEngine.playTactileClick();
                  reverbEngine.triggerImpulse();
                }}
                className="w-11 h-11 rounded-full bg-zinc-900 border border-zinc-700 text-amber-400 hover:text-white flex items-center justify-center active:scale-90 transition-transform"
                title="Trigger sharp transient impulse to audition tail in silence"
              >
                <Sparkles className="w-4 h-4" />
              </button>
            </div>

            {/* THE ICONIC CENTER SILICONE PLAY BUTTON */}
            {/* Engaging PLAY flips to Set 2 (Tone Modulation) on the light strips! */}
            <div className="flex flex-col items-center">
              <button
                onClick={handleTogglePlay}
                className={`w-20 h-20 rounded-full border-2 flex flex-col items-center justify-center transition-all duration-150 relative active:scale-95 shadow-2xl select-none ${
                  isPlaying
                    ? 'bg-[#ff5722] border-[#ff7a50] text-zinc-950 shadow-[0_0_28px_rgba(255,87,34,0.7)]'
                    : 'bg-zinc-800 border-zinc-600 text-zinc-200 hover:border-zinc-500 shadow-lg'
                }`}
                title="Press PLAY to start audio and switch light strips to real-time tone modulation!"
              >
                <div className="absolute inset-1.5 rounded-full border border-black/20 pointer-events-none" />
                <span
                  className={`w-2.5 h-2.5 rounded-full mb-1 transition-all ${
                    isPlaying
                      ? 'bg-white shadow-[0_0_8px_#ffffff] animate-ping'
                      : 'bg-zinc-500'
                  }`}
                />
                {isPlaying ? (
                  <Square className="w-5 h-5 fill-current" />
                ) : (
                  <Play className="w-5 h-5 fill-current ml-0.5" />
                )}
                <span className="text-[9px] font-mono font-bold mt-0.5">
                  {isPlaying ? 'STOP' : 'PLAY'}
                </span>
              </button>
            </div>

            {/* Right Controls: Stems Mode Toggle & Auto-Switch Check */}
            <div className="flex items-center gap-2">
              <button
                onClick={handleToggleStemsMode}
                className={`w-11 h-11 rounded-full border flex items-center justify-center font-mono font-bold text-xs transition-all ${
                  stemsMode
                    ? 'bg-amber-400 text-zinc-950 border-amber-400 shadow-[0_0_10px_#f59e0b]'
                    : 'bg-zinc-900 border-zinc-700 text-zinc-400 hover:text-zinc-200'
                }`}
                title="Toggle 4-Stems Volume Control"
              >
                STEM
              </button>

              <label
                className="w-11 h-11 rounded-full bg-zinc-900 border border-zinc-700 flex items-center justify-center cursor-pointer text-zinc-400 hover:text-zinc-200"
                title="Auto-switch light strips to tone modulation when Play is pressed"
              >
                <input
                  type="checkbox"
                  checked={autoSwitchOnPlay}
                  onChange={() => setAutoSwitchOnPlay(!autoSwitchOnPlay)}
                  className="rounded bg-zinc-800 border-zinc-600 text-[#ff5722] focus:ring-0 w-3.5 h-3.5"
                />
              </label>
            </div>

          </div>

        </div>

      </main>

      {/* OFF-DEVICE DOCUMENTATION MODAL (timknapen/SP-1-dev & solderless.engineering architecture) */}
      {showDocModal && (
        <div className="fixed inset-0 z-50 bg-black/80 backdrop-blur-sm flex items-center justify-center p-4">
          <div className="bg-zinc-900 border border-zinc-700 rounded-3xl p-6 max-w-xl w-full shadow-2xl font-mono text-xs max-h-[85vh] overflow-y-auto">
            <div className="flex items-center justify-between pb-3 mb-4 border-b border-zinc-800">
              <h3 className="text-sm font-bold text-[#ff5722] uppercase tracking-wider">
                Teenage Engineering SP-1 Hardware Spec
              </h3>
              <button
                onClick={() => setShowDocModal(false)}
                className="text-zinc-400 hover:text-zinc-200"
              >
                ✕ CLOSE
              </button>
            </div>

            <div className="space-y-4 text-zinc-300 leading-relaxed">
              <p>
                Based on <strong className="text-white">timknapen/SP-1-dev</strong> and flashable via <strong className="text-white">solderless.engineering</strong>:
              </p>

              <div className="p-3 rounded-xl bg-zinc-950 border border-zinc-800 space-y-1 text-[11px]">
                <div className="text-white font-bold mb-1">HARDWARE ARCHITECTURE:</div>
                <div>· <strong className="text-zinc-200">MCU:</strong> Nordic nRF52840 (ARM Cortex-M4 @ 64 MHz, 256 KB RAM)</div>
                <div>· <strong className="text-zinc-200">Screen:</strong> NONE (100% Screenless Tactile Interaction)</div>
                <div>· <strong className="text-zinc-200">Display:</strong> 4 Touch-Sensitive Light Strips with LED segment ladders</div>
                <div>· <strong className="text-zinc-200">Audio In/Out:</strong> CS42L42 I2S Codec / USB Audio Class 2.0</div>
              </div>

              <div className="p-3 rounded-xl bg-zinc-950 border border-cyan-900/40 space-y-1 text-[11px]">
                <div className="text-cyan-400 font-bold mb-1">REVERB MAPPING:</div>
                <div>· <strong className="text-white">Set 1 (Core Reverb):</strong> 1: Mix, 2: Decay, 3: Diffusion, 4: HP/LP Filter</div>
                <div>· <strong className="text-white">Set 2 (Play Modulation):</strong> 1: Mod Depth, 2: Mod Rate, 3: Pre-Delay, 4: Vintage Grit</div>
                <div>· <strong className="text-white">Preset Rocker (◀ / ▶):</strong> Cycles the 8 historic early digital reverb algorithms</div>
              </div>
            </div>
          </div>
        </div>
      )}

      {/* USB-C LIVE AUDIO MODAL */}
      {showUsbModal && (
        <div className="fixed inset-0 z-50 bg-black/80 backdrop-blur-sm flex items-center justify-center p-4">
          <div className="bg-zinc-900 border border-zinc-700 rounded-3xl p-6 max-w-md w-full shadow-2xl font-mono text-xs">
            <div className="flex items-center justify-between pb-3 mb-4 border-b border-zinc-800">
              <div className="flex items-center gap-2">
                <Cable className="w-4 h-4 text-cyan-400" />
                <h3 className="text-sm font-bold text-cyan-400 uppercase tracking-wider">
                  USB-C Live Audio Ingest
                </h3>
              </div>
              <button
                onClick={() => setShowUsbModal(false)}
                className="text-zinc-400 hover:text-zinc-200"
              >
                ✕ CLOSE
              </button>
            </div>

            <div className="space-y-4 text-zinc-300">
              <p className="text-[11px] text-zinc-400 leading-relaxed">
                Connect your Teenage Engineering SP-1 over USB-C. Streams raw studio audio directly into the reverb tank with zero browser vocal suppression.
              </p>

              <div className="p-3 rounded-xl bg-zinc-950 border border-zinc-800 flex items-center justify-between">
                <div>
                  <div className="font-bold text-zinc-200">STREAM STATUS</div>
                  <div className="text-[10px] text-zinc-500">
                    {usbConfig.enabled ? `Connected: ${usbConfig.deviceName}` : 'Offline'}
                  </div>
                </div>
                <button
                  onClick={() => handleToggleUsbAudio()}
                  className={`px-4 py-2 rounded-lg font-bold transition-all ${
                    usbConfig.enabled
                      ? 'bg-red-500/20 text-red-400 border border-red-500'
                      : 'bg-cyan-500 text-zinc-950 hover:bg-cyan-400'
                  }`}
                >
                  {usbConfig.enabled ? 'DISCONNECT' : 'CONNECT USB'}
                </button>
              </div>

              {availableUsbDevices.length > 0 && (
                <div className="space-y-1.5 max-h-32 overflow-y-auto">
                  {availableUsbDevices.map(dev => (
                    <div
                      key={dev.deviceId}
                      className="p-2 rounded-lg border border-zinc-800 bg-zinc-950 text-[11px] truncate text-zinc-300"
                    >
                      {dev.label || 'USB Audio Device'}
                    </div>
                  ))}
                </div>
              )}

              {usbConfig.enabled && (
                <div className="p-3 rounded-xl bg-zinc-950 border border-zinc-800 flex items-center justify-between">
                  <span className="text-[10px] text-zinc-400">INPUT SIGNAL:</span>
                  <div className="flex-1 mx-3 h-2 rounded bg-zinc-900 overflow-hidden">
                    <div
                      className="h-full bg-cyan-400 transition-all duration-75"
                      style={{ width: `${Math.min(100, usbLevel * 100)}%` }}
                    />
                  </div>
                  <span className="text-[10px] text-zinc-400">{(usbLevel * 100).toFixed(0)}%</span>
                </div>
              )}
            </div>
          </div>
        </div>
      )}

      {/* Screenless Bottom Minimalist Status Footer */}
      <footer className="border-t border-zinc-900 px-6 py-2.5 flex items-center justify-between text-[11px] font-mono text-zinc-500">
        <div className="flex items-center gap-2">
          <span>TEENAGE ENGINEERING SP-1</span>
          <span>·</span>
          <span>SCREENLESS REVERB</span>
          <span>·</span>
          <span>timknapen/SP-1-dev</span>
        </div>
        <div className="flex items-center gap-3">
          <span>ALGO: #{currentPresetIndex}</span>
          <span>·</span>
          <span>USB: {usbConfig.enabled ? 'ACTIVE' : 'STANDBY'}</span>
          <span>·</span>
          <span>DSP LOAD: {dspLoad}%</span>
        </div>
      </footer>
    </div>
  );
}
