import React, { useRef, useEffect, useState } from 'react';
import { reverbEngine, ReverbParams } from '../audio/reverbEngine';

interface CrtVisualizerProps {
  params: ReverbParams;
  isPlaying: boolean;
  lowCpuMode: boolean;
}

export const CrtVisualizer: React.FC<CrtVisualizerProps> = ({ params, isPlaying, lowCpuMode }) => {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const [viewMode, setViewMode] = useState<'wave' | 'fft' | 'space'>('wave');
  const [colorMode, setColorMode] = useState<'te-orange' | 'amber' | 'mono'>('te-orange');
  const [isPaused, setIsPaused] = useState(false);

  useEffect(() => {
    let animId: number;
    let lastTime = 0;
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (!ctx) return;

    const dataArrayTime = new Uint8Array(lowCpuMode ? 128 : 256);
    const dataArrayFreq = new Uint8Array(lowCpuMode ? 128 : 256);

    const render = (time: number) => {
      animId = requestAnimationFrame(render);
      if (isPaused) return;

      // Throttle FPS for low CPU devices: ~30 FPS in lowCpuMode, 60 FPS otherwise
      const targetFps = lowCpuMode ? 30 : 60;
      const interval = 1000 / targetFps;
      if (time - lastTime < interval) return;
      lastTime = time;

      const width = canvas.width;
      const height = canvas.height;

      // Clear with background decay
      ctx.fillStyle = 'rgba(10, 10, 12, 0.35)';
      ctx.fillRect(0, 0, width, height);

      // Color scheme
      let strokeColor = '#ff5722'; // Teenage Engineering orange
      let fillColor = 'rgba(255, 87, 34, 0.25)';
      let glowColor = 'rgba(255, 87, 34, 0.5)';

      if (colorMode === 'amber') {
        strokeColor = '#f59e0b';
        fillColor = 'rgba(245, 158, 11, 0.25)';
        glowColor = 'rgba(245, 158, 11, 0.5)';
      } else if (colorMode === 'mono') {
        strokeColor = '#e4e4e7';
        fillColor = 'rgba(228, 228, 231, 0.2)';
        glowColor = 'rgba(255, 255, 255, 0.4)';
      }

      // Grid lines
      ctx.strokeStyle = 'rgba(255, 255, 255, 0.05)';
      ctx.lineWidth = 1;
      const gridX = 6;
      const gridY = 3;
      for (let i = 1; i < gridX; i++) {
        ctx.beginPath();
        ctx.moveTo((width / gridX) * i, 0);
        ctx.lineTo((width / gridX) * i, height);
        ctx.stroke();
      }
      for (let j = 1; j < gridY; j++) {
        ctx.beginPath();
        ctx.moveTo(0, (height / gridY) * j);
        ctx.lineTo(width, (height / gridY) * j);
        ctx.stroke();
      }

      if (reverbEngine.outputAnalyser) {
        reverbEngine.outputAnalyser.getByteTimeDomainData(dataArrayTime);
        reverbEngine.outputAnalyser.getByteFrequencyData(dataArrayFreq);
      }

      ctx.shadowBlur = lowCpuMode ? 0 : 6;
      ctx.shadowColor = glowColor;

      if (viewMode === 'wave') {
        // 1. Oscilloscope
        ctx.strokeStyle = strokeColor;
        ctx.lineWidth = 1.8;
        ctx.beginPath();

        const len = dataArrayTime.length;
        const sliceWidth = width / len;
        let x = 0;

        for (let i = 0; i < len; i++) {
          const v = dataArrayTime[i] / 128.0;
          const y = (v * height) / 2;
          if (i === 0) ctx.moveTo(x, y);
          else ctx.lineTo(x, y);
          x += sliceWidth;
        }
        ctx.stroke();
      } else if (viewMode === 'fft') {
        // 2. Frequency Spectrum & Cutoff marker
        const barCount = lowCpuMode ? 24 : 36;
        const barWidth = width / barCount - 1;

        for (let i = 0; i < barCount; i++) {
          const freqVal = dataArrayFreq[Math.floor((i / barCount) * (dataArrayFreq.length / 2))] || 0;
          const barHeight = (freqVal / 255) * (height - 10);
          const x = i * (barWidth + 1);
          const y = height - barHeight;

          ctx.fillStyle = fillColor;
          ctx.fillRect(x, y, barWidth, barHeight);
          ctx.fillStyle = strokeColor;
          ctx.fillRect(x, y, barWidth, 1.5);
        }

        // Draw Filter Marker
        const cutoffNorm = Math.log10(params.filterCutoff / 200) / Math.log10(16000 / 200);
        const filterX = Math.max(0, Math.min(width, cutoffNorm * width));
        ctx.strokeStyle = '#ef4444';
        ctx.lineWidth = 1;
        ctx.setLineDash([2, 2]);
        ctx.beginPath();
        ctx.moveTo(filterX, 0);
        ctx.lineTo(filterX, height);
        ctx.stroke();
        ctx.setLineDash([]);
      } else {
        // 3. Reverb Tail Diffusion Decay
        const numRays = lowCpuMode ? 16 : 24;
        const decayNorm = Math.min(1.0, params.decay / 20.0);
        const diffNorm = params.diffusion;

        ctx.strokeStyle = strokeColor;
        ctx.lineWidth = 1.2;

        for (let i = 0; i < numRays; i++) {
          const t = i / numRays;
          const x = t * width;
          const env = Math.exp(-t * (4 - decayNorm * 3));
          const jitter = (Math.sin(i * 12.3) * 0.5 + 0.5) * (1 - diffNorm) * 20;
          const y = height / 2 - (env * (height * 0.4) + jitter);
          const yBot = height / 2 + (env * (height * 0.4) + jitter);

          ctx.beginPath();
          ctx.moveTo(x, y);
          ctx.lineTo(x, yBot);
          ctx.stroke();
        }
      }

      ctx.shadowBlur = 0;
    };

    animId = requestAnimationFrame(render);
    return () => cancelAnimationFrame(animId);
  }, [viewMode, colorMode, params, lowCpuMode, isPaused]);

  return (
    <div className="relative rounded-lg bg-zinc-950 border border-zinc-800 p-2 shadow-inner">
      {/* Top Toolbar */}
      <div className="flex items-center justify-between pb-1.5 mb-1 px-1 border-b border-zinc-800 text-[10px] font-mono">
        <div className="flex items-center gap-1">
          <span className="text-zinc-500 font-bold uppercase tracking-wider mr-1">
            SCOPE:
          </span>
          {(['wave', 'fft', 'space'] as const).map(mode => (
            <button
              key={mode}
              onClick={() => setViewMode(mode)}
              className={`px-1.5 py-0.5 rounded transition-colors ${
                viewMode === mode
                  ? 'bg-zinc-800 text-zinc-100 font-bold border border-zinc-700'
                  : 'text-zinc-500 hover:text-zinc-300'
              }`}
            >
              {mode.toUpperCase()}
            </button>
          ))}
        </div>

        {/* Phosphor color switch & Pause toggle */}
        <div className="flex items-center gap-2">
          <button
            onClick={() => setIsPaused(!isPaused)}
            className={`px-1.5 py-0.5 rounded text-[9px] font-mono border ${
              isPaused
                ? 'bg-amber-500/20 text-amber-400 border-amber-500/40'
                : 'text-zinc-500 border-zinc-800 hover:text-zinc-300'
            }`}
            title="Pause scope to save CPU"
          >
            {isPaused ? 'PAUSED' : 'LIVE'}
          </button>

          <div className="flex items-center gap-1">
            <button
              onClick={() => setColorMode('te-orange')}
              title="Teenage Engineering Orange"
              className={`w-2.5 h-2.5 rounded-full bg-[#ff5722] transition-all ${
                colorMode === 'te-orange' ? 'ring-2 ring-white scale-110 shadow-[0_0_6px_#ff5722]' : 'opacity-40'
              }`}
            />
            <button
              onClick={() => setColorMode('amber')}
              title="Amber Phosphor"
              className={`w-2.5 h-2.5 rounded-full bg-amber-500 transition-all ${
                colorMode === 'amber' ? 'ring-2 ring-white scale-110 shadow-[0_0_6px_#f59e0b]' : 'opacity-40'
              }`}
            />
            <button
              onClick={() => setColorMode('mono')}
              title="Monochrome LCD"
              className={`w-2.5 h-2.5 rounded-full bg-zinc-200 transition-all ${
                colorMode === 'mono' ? 'ring-2 ring-white scale-110 shadow-[0_0_6px_#ffffff]' : 'opacity-40'
              }`}
            />
          </div>
        </div>
      </div>

      {/* Canvas */}
      <div className="relative w-full h-20 bg-black rounded overflow-hidden border border-zinc-800/80">
        <canvas
          ref={canvasRef}
          width={320}
          height={80}
          className="w-full h-full block"
        />
        {/* Subtle scanline effect */}
        <div className="absolute inset-0 bg-[linear-gradient(rgba(255,255,255,0.02)_50%,rgba(0,0,0,0.25)_50%)] bg-[length:100%_2px] pointer-events-none opacity-60" />
      </div>
    </div>
  );
};
