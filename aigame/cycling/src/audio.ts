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
