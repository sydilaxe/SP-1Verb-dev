import React, { useRef, useState, useEffect, useCallback } from 'react';

interface KnobProps {
  id: string;
  indexNumber: number; // 1, 2, 3, 4
  label: string;
  sublabel?: string;
  value: number; // current value in real units
  min: number;
  max: number;
  step?: number;
  unit: string;
  formatValue?: (val: number) => string;
  defaultValue: number;
  colorScheme: 'amber' | 'cyan';
  onChange: (val: number) => void;
  disabled?: boolean;
}

export const Knob: React.FC<KnobProps> = ({
  indexNumber,
  label,
  sublabel,
  value,
  min,
  max,
  step = 0.01,
  unit,
  formatValue,
  defaultValue,
  colorScheme,
  onChange,
  disabled = false,
}) => {
  const knobRef = useRef<HTMLDivElement>(null);
  const [isDragging, setIsDragging] = useState(false);
  const [isHovered, setIsHovered] = useState(false);
  const dragStartY = useRef(0);
  const startVal = useRef(value);

  // Normalize 0 to 1
  const normalized = Math.max(0, Math.min(1, (value - min) / (max - min)));

  // Angles: -135deg (min) to +135deg (max) => 270 degree total sweep
  const angle = -135 + normalized * 270;

  const handlePointerDown = (e: React.PointerEvent) => {
    if (disabled) return;
    setIsDragging(true);
    dragStartY.current = e.clientY;
    startVal.current = value;
    (e.target as HTMLElement).setPointerCapture?.(e.pointerId);
  };

  const handlePointerMove = useCallback((e: React.PointerEvent) => {
    if (!isDragging || disabled) return;
    const dy = dragStartY.current - e.clientY; // positive = dragging up
    const isFine = e.shiftKey;
    const sensitivity = isFine ? 400 : 150; // px for full range

    const deltaNorm = dy / sensitivity;
    const newNorm = Math.max(0, Math.min(1, (startVal.current - min) / (max - min) + deltaNorm));
    let newVal = min + newNorm * (max - min);

    if (step) {
      newVal = Math.round(newVal / step) * step;
    }
    newVal = Math.max(min, Math.min(max, newVal));
    onChange(newVal);
  }, [isDragging, disabled, min, max, step, onChange]);

  const handlePointerUp = (e: React.PointerEvent) => {
    if (isDragging) {
      setIsDragging(false);
      try {
        (e.target as HTMLElement).releasePointerCapture?.(e.pointerId);
      } catch {
        /* ignore */
      }
    }
  };

  const handleWheel = (e: React.WheelEvent) => {
    if (disabled) return;
    e.preventDefault();
    const delta = -Math.sign(e.deltaY) * ((max - min) / 60);
    let newVal = Math.max(min, Math.min(max, value + delta));
    if (step) newVal = Math.round(newVal / step) * step;
    onChange(newVal);
  };

  const handleDoubleClick = () => {
    if (disabled) return;
    onChange(defaultValue);
  };

  // Color mappings
  const theme = colorScheme === 'amber'
    ? {
        accent: '#f59e0b',
        glow: 'rgba(245, 158, 11, 0.45)',
        ringTrack: 'rgba(245, 158, 11, 0.15)',
        badgeBg: 'bg-amber-500/10 border-amber-500/30 text-amber-400',
        dotColor: 'bg-amber-400 shadow-[0_0_8px_#f59e0b]',
        readout: 'text-amber-400 font-mono',
      }
    : {
        accent: '#06b6d4',
        glow: 'rgba(6, 182, 212, 0.45)',
        ringTrack: 'rgba(6, 182, 212, 0.15)',
        badgeBg: 'bg-cyan-500/10 border-cyan-500/30 text-cyan-400',
        dotColor: 'bg-cyan-400 shadow-[0_0_8px_#06b6d4]',
        readout: 'text-cyan-400 font-mono',
      };

  const displayVal = formatValue ? formatValue(value) : (value >= 100 ? value.toFixed(0) : value.toFixed(1));

  // SVG arc calculation for 270 degree sweep
  const radius = 38;
  const strokeWidth = 3.5;
  const circumference = 2 * Math.PI * radius;
  const arcLength = circumference * (270 / 360);
  const strokeDashoffset = arcLength - normalized * arcLength;

  return (
    <div
      className={`flex flex-col items-center select-none group transition-all duration-200 ${
        disabled ? 'opacity-40 pointer-events-none' : ''
      }`}
      onWheel={handleWheel}
      onMouseEnter={() => setIsHovered(true)}
      onMouseLeave={() => setIsHovered(false)}
    >
      {/* Top Header: Hardware Control Index and Label */}
      <div className="flex items-center gap-1.5 mb-2">
        <span
          className={`flex items-center justify-center w-5 h-5 rounded text-[10px] font-bold border ${theme.badgeBg}`}
        >
          {indexNumber}
        </span>
        <span className="text-xs font-semibold tracking-wider uppercase text-zinc-300">
          {label}
        </span>
      </div>

      {/* Dial Container */}
      <div
        ref={knobRef}
        onPointerDown={handlePointerDown}
        onPointerMove={handlePointerMove}
        onPointerUp={handlePointerUp}
        onPointerCancel={handlePointerUp}
        onDoubleClick={handleDoubleClick}
        className="relative w-24 h-24 flex items-center justify-center cursor-ns-resize touch-none"
        title={`${label}: ${displayVal} ${unit} (Drag up/down or use mouse wheel. Double-click to reset)`}
      >
        {/* SVG Arc LED meter */}
        <svg
          className="absolute inset-0 w-full h-full -rotate-90 pointer-events-none"
          viewBox="0 0 96 96"
        >
          {/* Background Arc Track */}
          <circle
            cx="48"
            cy="48"
            r={radius}
            fill="none"
            stroke="currentColor"
            strokeWidth={strokeWidth}
            strokeDasharray={`${arcLength} ${circumference}`}
            strokeDashoffset={0}
            strokeLinecap="round"
            className="text-zinc-800"
            style={{ transformOrigin: 'center', transform: 'rotate(-45deg)' }}
          />
          {/* Active Glowing Value Arc */}
          <circle
            cx="48"
            cy="48"
            r={radius}
            fill="none"
            stroke={theme.accent}
            strokeWidth={strokeWidth}
            strokeDasharray={`${arcLength} ${circumference}`}
            strokeDashoffset={strokeDashoffset}
            strokeLinecap="round"
            style={{
              transformOrigin: 'center',
              transform: 'rotate(-45deg)',
              filter: isDragging || isHovered ? `drop-shadow(0 0 6px ${theme.glow})` : 'none',
              transition: isDragging ? 'none' : 'stroke-dashoffset 0.08s ease-out',
            }}
          />
        </svg>

        {/* Outer Knurled Bezel & Machined Cap */}
        <div
          className={`relative w-16 h-16 rounded-full bg-gradient-to-b from-zinc-700 via-zinc-800 to-zinc-950 p-[2px] shadow-lg transition-transform ${
            isDragging ? 'scale-105 shadow-xl' : 'hover:scale-[1.02]'
          }`}
          style={{
            boxShadow: isDragging
              ? `0 0 20px ${theme.glow}, inset 0 2px 3px rgba(255,255,255,0.2)`
              : '0 4px 12px rgba(0,0,0,0.6), inset 0 1px 2px rgba(255,255,255,0.15)',
          }}
        >
          {/* Inner Machined Faceplate */}
          <div
            className="w-full h-full rounded-full bg-gradient-to-br from-zinc-800 via-zinc-900 to-zinc-950 flex items-center justify-center relative overflow-hidden"
            style={{
              transform: `rotate(${angle}deg)`,
              transition: isDragging ? 'none' : 'transform 0.08s ease-out',
            }}
          >
            {/* Concentric brushed metallic texture */}
            <div className="absolute inset-1 rounded-full border border-zinc-700/50 bg-[radial-gradient(ellipse_at_center,_var(--tw-gradient-stops))] from-zinc-700/40 via-zinc-900/60 to-black pointer-events-none" />

            {/* Tactile Pointer Notch / Line */}
            <div
              className={`absolute top-1 w-1 h-3.5 rounded-full ${theme.dotColor}`}
            />

            {/* Center Cap Screw Accent */}
            <div className="w-4 h-4 rounded-full bg-gradient-to-b from-zinc-700 to-zinc-900 border border-zinc-600/60 shadow-inner flex items-center justify-center">
              <div className="w-1.5 h-[1.5px] bg-zinc-500 rotate-45" />
            </div>
          </div>
        </div>
      </div>

      {/* Value Readout & Unit */}
      <div className="mt-1 flex flex-col items-center">
        <div className="px-2 py-0.5 rounded bg-black/60 border border-zinc-800/80 flex items-baseline gap-1 shadow-inner">
          <span className={`text-xs font-bold tracking-tight ${theme.readout}`}>
            {displayVal}
          </span>
          <span className="text-[10px] font-mono text-zinc-500 uppercase">{unit}</span>
        </div>
        {sublabel && (
          <span className="text-[9px] text-zinc-500 tracking-wider uppercase mt-1">
            {sublabel}
          </span>
        )}
      </div>
    </div>
  );
};
