const CHORDS: number[][] = [
  [220.0, 261.63, 329.63],
  [174.61, 220.0, 261.63],
  [130.81, 196.0, 329.63],
  [196.0, 246.94, 293.66],
];
const CHORD_SECONDS = 4.8;
const VOLUME = 0.14;

export class Music {
  private ctx: AudioContext | null = null;
  private master: GainNode | null = null;
  private timer: number | null = null;
  private step = 0;
  private volume = VOLUME;

  start(): void {
    if (!this.ctx) {
      this.ctx = new AudioContext();
      this.master = this.ctx.createGain();
      this.master.gain.value = this.volume;
      this.master.connect(this.ctx.destination);
    }
    void this.ctx.resume();
    if (this.timer === null) this.next();
  }

  toggle(): boolean {
    if (!this.ctx || !this.master) return false;
    this.volume = this.volume > 0 ? 0 : VOLUME;
    this.master.gain.setTargetAtTime(this.volume, this.ctx.currentTime, 0.1);
    return this.volume === 0;
  }

  private next(): void {
    this.playChord(CHORDS[this.step % CHORDS.length]);
    this.step++;
    this.timer = window.setTimeout(() => this.next(), CHORD_SECONDS * 1000);
  }

  private playChord(notes: number[]): void {
    const ctx = this.ctx!;
    const now = ctx.currentTime;
    const bus = ctx.createGain();
    const filter = ctx.createBiquadFilter();
    filter.type = 'lowpass';
    filter.frequency.value = 1400;
    bus.connect(filter);
    filter.connect(this.master!);
    const chordEnd = now + CHORD_SECONDS + 1.2;
    bus.gain.setValueAtTime(0, now);
    bus.gain.linearRampToValueAtTime(1, now + 1.4);
    bus.gain.setValueAtTime(1, now + CHORD_SECONDS - 0.4);
    bus.gain.linearRampToValueAtTime(0, chordEnd);
    const bass = ctx.createOscillator();
    bass.type = 'sine';
    bass.frequency.value = notes[0] / 2;
    const bassGain = ctx.createGain();
    bassGain.gain.value = 0.45;
    bass.connect(bassGain);
    bassGain.connect(bus);
    bass.start(now);
    bass.stop(chordEnd);
    notes.forEach((f, i) => {
      const osc = ctx.createOscillator();
      osc.type = 'triangle';
      osc.frequency.value = f;
      osc.detune.value = (i - 1) * 4;
      const g = ctx.createGain();
      g.gain.value = 0.3;
      osc.connect(g);
      g.connect(bus);
      osc.start(now);
      osc.stop(chordEnd);
    });
  }
}
