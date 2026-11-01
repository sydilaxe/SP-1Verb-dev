import React, { useEffect, useState } from 'react';
import { reverbEngine } from '../audio/reverbEngine';

interface VuMeterProps {
  label: string;
}

export const VuMeter: React.FC<VuMeterProps> = ({ label }) => {
  const [levels, setLevels] = useState({ inL: 0, inR: 0, outL: 0, outR: 0 });

  useEffect(() => {
    let animId: number;
    const poll = () => {
      animId = requestAnimationFrame(poll);
      const m = reverbEngine.getMeterLevels();
      setLevels(prev => ({
        inL: Math.max(m.inL, prev.inL * 0.92),
        inR: Math.max(m.inR, prev.inR * 0.92),
        outL: Math.max(m.outL, prev.outL * 0.92),
        outR: Math.max(m.outR, prev.outR * 0.92),
      }));
    };
    poll();
    return () => cancelAnimationFrame(animId);
  }, []);

  const totalSegments = 12;

  const renderMeterBar = (val: number) => {
    const activeCount = Math.round(val * totalSegments);
    return (
      <div className="flex flex-col-reverse gap-[2px] w-2 h-20 bg-zinc-950 p-[2px] rounded border border-zinc-800">
        {Array.from({ length: totalSegments }).map((_, i) => {
          const isActive = i < activeCount;
          let color = 'bg-emerald-500 shadow-[0_0_4px_#10b981]';
          if (i >= 8 && i < 10) color = 'bg-amber-400 shadow-[0_0_4px_#f59e0b]';
          if (i >= 10) color = 'bg-red-500 shadow-[0_0_4px_#ef4444]';

          return (
            <div
              key={i}
              className={`w-full h-1 rounded-[1px] transition-all duration-75 ${
                isActive ? color : 'bg-zinc-800/50'
              }`}
            />
          );
        })}
      </div>
    );
  };

  return (
    <div className="flex flex-col items-center select-none">
      <span className="text-[10px] font-mono font-semibold tracking-wider text-zinc-400 mb-1.5 uppercase">
        {label}
      </span>
      <div className="flex items-center gap-1.5 p-1 bg-zinc-900/80 rounded border border-zinc-800">
        <div className="flex flex-col items-center">
          {renderMeterBar(levels.outL)}
          <span className="text-[8px] font-mono text-zinc-500 mt-1">L</span>
        </div>
        <div className="flex flex-col items-center">
          {renderMeterBar(levels.outR)}
          <span className="text-[8px] font-mono text-zinc-500 mt-1">R</span>
        </div>
      </div>
    </div>
  );
};
