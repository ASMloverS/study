export type MusicTier = 'calm' | 'intense' | 'sprint';

const FILES: Record<MusicTier, string> = {
  calm: '/music/calm.mp3',
  intense: '/music/intense.mp3',
  sprint: '/music/sprint.mp3',
};
const TIERS: MusicTier[] = ['calm', 'intense', 'sprint'];

export class Music {
  private ctx: AudioContext | null = null;
  private master: GainNode | null = null;
  private windGain: GainNode | null = null;
  private windFilter: BiquadFilterNode | null = null;
  private gains = new Map<MusicTier, GainNode>();
  private tier: MusicTier = 'calm';
  private volume = 0.35;
  private started = false;

  start(): void {
    if (this.started) return;
    this.started = true;
    this.ctx = new AudioContext();
    this.master = this.ctx.createGain();
    this.master.gain.value = this.volume;
    this.master.connect(this.ctx.destination);
    const len = 2 * this.ctx.sampleRate;
    const buf = this.ctx.createBuffer(1, len, this.ctx.sampleRate);
    const data = buf.getChannelData(0);
    for (let i = 0; i < len; i++) data[i] = Math.random() * 2 - 1;
    const src = this.ctx.createBufferSource();
    src.buffer = buf;
    src.loop = true;
    this.windFilter = this.ctx.createBiquadFilter();
    this.windFilter.type = 'bandpass';
    this.windFilter.frequency.value = 300;
    this.windFilter.Q.value = 0.8;
    this.windGain = this.ctx.createGain();
    this.windGain.gain.value = 0;
    src.connect(this.windFilter);
    this.windFilter.connect(this.windGain);
    this.windGain.connect(this.master);
    src.start();
    void this.load();
  }

  toggle(): boolean {
    if (!this.ctx || !this.master) return false;
    this.volume = this.volume > 0 ? 0 : 0.35;
    this.master.gain.setTargetAtTime(this.volume, this.ctx.currentTime, 0.1);
    return this.volume === 0;
  }

  setTier(tier: MusicTier): void {
    if (tier === this.tier) return;
    this.tier = tier;
    this.applyTier();
  }

  setWind(speed: number): void {
    if (!this.ctx || !this.windGain || !this.windFilter) return;
    this.windGain.gain.setTargetAtTime(Math.min(0.5, (speed / 40) ** 2), this.ctx.currentTime, 0.1);
    this.windFilter.frequency.setTargetAtTime(300 + speed * 30, this.ctx.currentTime, 0.1);
  }

  blip(): void {
    if (!this.ctx) return;
    const osc = this.ctx.createOscillator();
    const gain = this.ctx.createGain();
    osc.frequency.setValueAtTime(880, this.ctx.currentTime);
    osc.frequency.exponentialRampToValueAtTime(1760, this.ctx.currentTime + 0.12);
    gain.gain.setValueAtTime(0.25, this.ctx.currentTime);
    gain.gain.exponentialRampToValueAtTime(0.001, this.ctx.currentTime + 0.15);
    osc.connect(gain);
    gain.connect(this.master!);
    osc.start();
    osc.stop(this.ctx.currentTime + 0.16);
  }

  private applyTier(): void {
    const now = this.ctx?.currentTime ?? 0;
    this.gains.forEach((g, tier) => {
      g.gain.setTargetAtTime(tier === this.tier ? 1 : 0, now, 0.3);
    });
  }

  private async load(): Promise<void> {
    const ctx = this.ctx!;
    for (const tier of TIERS) {
      try {
        const res = await fetch(FILES[tier]);
        if (!res.ok) continue;
        const buf = await ctx.decodeAudioData(await res.arrayBuffer());
        const src = ctx.createBufferSource();
        src.buffer = buf;
        src.loop = true;
        const g = ctx.createGain();
        g.gain.value = 0;
        src.connect(g);
        g.connect(this.master!);
        src.start();
        this.gains.set(tier, g);
        this.applyTier();
      } catch {
        continue;
      }
    }
  }
}
