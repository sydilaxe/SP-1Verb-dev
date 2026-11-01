import React, { useRef, useState, useCallback } from 'react';

interface TouchLightStripProps {
  id: string;
  indexNumber: number; // 1, 2, 3, 4
  label: string;
  sublabel: string;
  value: number; // current value
  min: number;
  max: number;
  step?: number;
  unit: string;
  formatValue?: (val: number) => string;
  colorScheme: 'white' | 'orange' | 'cyan' | 'stem';
  stemColor?: string;
  onChange: (val: number) => void;
  onDoubleClick?: () => void;
  disabled?: boolean;
  lowCpuMode?: boolean;
}

export const TouchLightStrip: React.FC<TouchLightStripProps> = ({
  indexNumber,
  label,
  sublabel,
  value,
  min,
  max,
  step = 0.01,
  unit,
  formatValue,
  colorScheme,
  stemColor,
  onChange,
  onDoubleClick,
  disabled = false,
  lowCpuMode = false,
}) => {
  const stripRef = useRef<HTMLDivElement>(null);
  const [isInteracting, setIsInteracting] = useState(false);

  // Normalize value 0 to 1
  const normalized = Math.max(0, Math.min(1, (value - min) / (max - min)));

  // Total LED segments on the light strip (16 segments for classic Stem Player look)
  const totalLeds = lowCpuMode ? 12 : 16;
  const activeLedsCount = Math.round(normalized * totalLeds);

  const calculateValueFromPointer = useCallback((clientY: number) => {
    if (!stripRef.current) return;
    const rect = stripRef.current.getBoundingClientRect();
    // Invert: top = 1.0, bottom = 0.0
    const rawRatio = 1.0 - (clientY - rect.top) / rect.height;
    const clampedRatio = Math.max(0, Math.min(1, rawRatio));
    let newVal = min + clampedRatio * (max - min);
    if (step) {
      newVal = Math.round(newVal / step) * step;
    }
    onChange(Math.max(min, Math.min(max, newVal)));
  }, [min, max, step, onChange]);

  const handlePointerDown = (e: React.PointerEvent) => {
    if (disabled) return;
    setIsInteracting(true);
    (e.target as HTMLElement).setPointerCapture?.(e.pointerId);
    calculateValueFromPointer(e.clientY);
  };

  const handlePointerMove = (e: React.PointerEvent) => {
    if (!isInteracting || disabled) return;
    calculateValueFromPointer(e.clientY);
  };

  const handlePointerUp = (e: React.PointerEvent) => {
    if (isInteracting) {
      setIsInteracting(false);
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
    const delta = -Math.sign(e.deltaY) * ((max - min) / 40);
    let newVal = Math.max(min, Math.min(max, value + delta));
    if (step) newVal = Math.round(newVal / step) * step;
    onChange(newVal);
  };

  // Determine LED styling based on color scheme
  const getLedStyle = (i: number) => {
    const isActive = i < activeLedsCount;
    const isTopActive = i === activeLedsCount - 1;

    if (!isActive) {
      return 'bg-zinc-800/60 border-zinc-700/40 opacity-40';
    }

    if (colorScheme === 'orange') {
      return isTopActive
        ? 'bg-[#ff5722] border-[#ff7a50] shadow-[0_0_10px_#ff5722]'
        : 'bg-[#ff5722]/85 border-[#ff5722]/60 shadow-[0_0_4px_rgba(255,87,34,0.4)]';
    }

    if (colorScheme === 'cyan') {
      return isTopActive
        ? 'bg-[#00f0ff] border-[#66f7ff] shadow-[0_0_10px_#00f0ff]'
        : 'bg-[#00f0ff]/80 border-[#00f0ff]/60 shadow-[0_0_4px_rgba(0,240,255,0.4)]';
    }

    if (colorScheme === 'stem' && stemColor) {
      return isTopActive
        ? `${stemColor} shadow-[0_0_10px_currentColor]`
        : `${stemColor} opacity-80`;
    }

    // Default 'white' (Warm bone white typical of Kano/TE Stem Player)
    return isTopActive
      ? 'bg-amber-100 border-white shadow-[0_0_10px_rgba(255,255,255,0.9)]'
      : 'bg-amber-200/80 border-amber-100/50 shadow-[0_0_4px_rgba(255,245,225,0.4)]';
  };

  const displayVal = formatValue ? formatValue(value) : (value >= 100 ? value.toFixed(0) : value.toFixed(1));

  return (
    <div
      className={`flex flex-col items-center select-none group transition-opacity ${
        disabled ? 'opacity-40 pointer-events-none' : ''
      }`}
      onWheel={handleWheel}
      onDoubleClick={onDoubleClick}
    >
      {/* Top Channel Header: Number badge and label */}
      <div className="flex flex-col items-center mb-2.5 text-center">
        <div className="flex items-center gap-1.5 mb-0.5">
          <span
            className={`flex items-center justify-center w-5 h-5 rounded-sm font-mono text-[10px] font-bold border transition-colors ${
              colorScheme === 'orange'
                ? 'bg-[#ff5722]/20 border-[#ff5722] text-[#ff7a50]'
                : colorScheme === 'cyan'
                ? 'bg-[#00f0ff]/20 border-[#00f0ff] text-[#00f0ff]'
                : 'bg-zinc-800 border-zinc-600 text-zinc-300'
            }`}
          >
            {indexNumber}
          </span>
          <span className="font-mono text-xs font-bold tracking-wider uppercase text-zinc-200">
            {label}
          </span>
        </div>
        <span className="text-[9px] font-mono text-zinc-500 uppercase tracking-tight">
          {sublabel}
        </span>
      </div>

      {/* Touch-Sensitive Light Strip (The Iconic TE Stem Player Light Slider) */}
      <div
        ref={stripRef}
        onPointerDown={handlePointerDown}
        onPointerMove={handlePointerMove}
        onPointerUp={handlePointerUp}
        onPointerCancel={handlePointerUp}
        className={`relative w-12 sm:w-14 h-56 sm:h-64 rounded-full bg-zinc-950/90 border border-zinc-800 p-2 flex flex-col justify-between items-center cursor-pointer touch-none shadow-inner transition-all duration-150 ${
          isInteracting ? 'ring-2 ring-zinc-500/50 scale-[1.02]' : 'hover:border-zinc-700'
        }`}
        title={`${label}: ${displayVal} ${unit} (Touch or drag up/down)`}
      >
        {/* Soft silicone track groove background */}
        <div className="absolute inset-y-2 inset-x-2 rounded-full bg-zinc-900/80 pointer-events-none" />

        {/* LED Light Ladder (reversed: index 0 is bottom, index N-1 is top) */}
        <div className="relative z-10 w-full h-full flex flex-col-reverse justify-between items-center py-1 pointer-events-none">
          {Array.from({ length: totalLeds }).map((_, i) => (
            <div
              key={i}
              className={`w-7 sm:w-8 h-2 sm:h-2.5 rounded-full border transition-all duration-75 ${getLedStyle(i)}`}
            />
          ))}
        </div>

        {/* Floating Touch Position Indicator Bead when interacting */}
        {isInteracting && (
          <div
            className="absolute left-1/2 -translate-x-1/2 w-9 h-3 rounded-full border-2 border-white bg-white/40 shadow-[0_0_12px_#ffffff] pointer-events-none z-20"
            style={{
              bottom: `${Math.max(4, Math.min(94, normalized * 100))}%`,
              transition: 'bottom 0.04s ease-out',
            }}
          />
        )}
      </div>

      {/* Value Readout Box */}
      <div className="mt-2.5 px-2.5 py-1 rounded-sm bg-zinc-900/90 border border-zinc-800 flex items-baseline gap-1 font-mono shadow-sm">
        <span
          className={`text-xs font-bold ${
            colorScheme === 'orange'
              ? 'text-[#ff5722]'
              : colorScheme === 'cyan'
              ? 'text-[#00f0ff]'
              : 'text-zinc-100'
          }`}
        >
          {displayVal}
        </span>
        <span className="text-[10px] text-zinc-500 uppercase">{unit}</span>
      </div>
    </div>
  );
};
