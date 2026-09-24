export type SoundKind =
  | 'ar_shot'
  | 'sg_shot'
  | 'sr_shot'
  | 'hit'
  | 'headshot'
  | 'kill'
  | 'reload'
  | 'switch'
  | 'empty'
  | 'step'
  | 'ui'
  | 'explode'
  | 'break'
  | 'melee'
  | 'throw'
  | 'flash'
  | 'streak'
  | 'uav';

export class AudioSys {
  private ctx: AudioContext | null = null;
  private master: GainNode | null = null;
  private noise: AudioBuffer | null = null;
  private volume = 0.7;
  private ttsOn = true;

  setTts(on: boolean): void {
    this.ttsOn = on;
    if (!on && typeof speechSynthesis !== 'undefined') speechSynthesis.cancel();
  }

  /** 中文语音播报（Web Speech API）；不可用时静默回退 */
  announce(text: string): void {
    if (!this.ttsOn || typeof speechSynthesis === 'undefined') return;
    try {
      speechSynthesis.cancel();
      const u = new SpeechSynthesisUtterance(text);
      u.lang = 'zh-CN';
      u.rate = 1.1;
      u.pitch = 1;
      speechSynthesis.speak(u);
    } catch {
      void 0;
    }
  }

  resume(): void {
    if (!this.ctx) {
      this.ctx = new AudioContext();
      this.master = this.ctx.createGain();
      this.master.gain.value = this.volume;
      this.master.connect(this.ctx.destination);
      const len = this.ctx.sampleRate;
      this.noise = this.ctx.createBuffer(1, len, this.ctx.sampleRate);
      const data = this.noise.getChannelData(0);
      for (let i = 0; i < len; i++) data[i] = Math.random() * 2 - 1;
    }
    if (this.ctx.state === 'suspended') void this.ctx.resume();
  }

  setVolume(v: number): void {
    this.volume = v;
    if (this.master) this.master.gain.value = v;
  }

  setListener(x: number, y: number, z: number, fx: number, fz: number): void {
    if (!this.ctx) return;
    const l = this.ctx.listener;
    if (l.positionX) {
      l.positionX.value = x;
      l.positionY.value = y;
      l.positionZ.value = z;
      l.forwardX.value = fx;
      l.forwardY.value = 0;
      l.forwardZ.value = fz;
      l.upX.value = 0;
      l.upY.value = 1;
      l.upZ.value = 0;
    }
  }

  playLocal(kind: SoundKind): void {
    if (!this.ctx || !this.master) return;
    this.emit(kind, this.master);
  }

  playAt(kind: SoundKind, x: number, y: number, z: number): void {
    if (!this.ctx || !this.master) return;
    const panner = this.ctx.createPanner();
    panner.panningModel = 'equalpower';
    panner.distanceModel = 'inverse';
    panner.refDistance = 4;
    panner.maxDistance = 80;
    panner.rolloffFactor = 1.4;
    panner.positionX.value = x;
    panner.positionY.value = y;
    panner.positionZ.value = z;
    panner.connect(this.master);
    this.emit(kind, panner);
  }

  private emit(kind: SoundKind, dest: AudioNode): void {
    const ctx = this.ctx!;
    const t = ctx.currentTime;
    switch (kind) {
      case 'ar_shot':
        this.burst(dest, t, 2400, 0.16, 0.3);
        this.thump(dest, t, 110, 0.05, 0.22);
        break;
      case 'sg_shot':
        this.burst(dest, t, 1100, 0.3, 0.42);
        this.thump(dest, t, 62, 0.12, 0.32);
        break;
      case 'sr_shot':
        this.burst(dest, t, 1900, 0.34, 0.42);
        this.thump(dest, t, 88, 0.1, 0.3);
        break;
      case 'hit':
        this.blip(dest, t, 880, 0.045, 0.16, 'triangle');
        break;
      case 'headshot':
        this.blip(dest, t, 1318, 0.06, 0.18, 'triangle');
        break;
      case 'kill':
        this.blip(dest, t, 660, 0.07, 0.16, 'square');
        this.blip(dest, t + 0.08, 990, 0.09, 0.16, 'square');
        break;
      case 'reload':
        this.blip(dest, t, 1900, 0.02, 0.1, 'square');
        this.blip(dest, t + 0.35, 1500, 0.02, 0.1, 'square');
        this.blip(dest, t + 1.2, 2300, 0.025, 0.12, 'square');
        break;
      case 'switch':
        this.blip(dest, t, 1400, 0.02, 0.1, 'square');
        this.burst(dest, t + 0.02, 3000, 0.06, 0.06);
        break;
      case 'empty':
        this.blip(dest, t, 1500, 0.03, 0.12, 'square');
        break;
      case 'step':
        this.burst(dest, t, 480, 0.05, 0.13);
        break;
      case 'ui':
        this.blip(dest, t, 740, 0.03, 0.1, 'triangle');
        break;
      case 'explode':
        this.burst(dest, t, 900, 0.65, 0.55);
        this.thump(dest, t, 52, 0.45, 0.5);
        this.thump(dest, t + 0.06, 90, 0.25, 0.3);
        break;
      case 'break':
        this.burst(dest, t, 2600, 0.14, 0.24);
        this.blip(dest, t + 0.02, 320, 0.04, 0.12, 'square');
        break;
      case 'melee':
        this.burst(dest, t, 900, 0.12, 0.2);
        this.blip(dest, t + 0.1, 240, 0.06, 0.14, 'square');
        break;
      case 'throw':
        this.burst(dest, t, 1400, 0.09, 0.12);
        break;
      case 'flash':
        this.blip(dest, t, 3200, 0.5, 0.3, 'sine');
        this.blip(dest, t + 0.02, 2400, 0.6, 0.2, 'sine');
        break;
      case 'streak':
        this.blip(dest, t, 620, 0.08, 0.18, 'triangle');
        this.blip(dest, t + 0.1, 930, 0.12, 0.18, 'triangle');
        break;
      case 'uav':
        this.blip(dest, t, 440, 0.25, 0.12, 'sawtooth');
        this.blip(dest, t + 0.3, 440, 0.25, 0.12, 'sawtooth');
        break;
    }
  }

  private burst(dest: AudioNode, t: number, cutoff: number, dur: number, gain: number): void {
    const ctx = this.ctx!;
    const src = ctx.createBufferSource();
    src.buffer = this.noise;
    src.loop = true;
    const filter = ctx.createBiquadFilter();
    filter.type = 'lowpass';
    filter.frequency.setValueAtTime(cutoff, t);
    filter.frequency.exponentialRampToValueAtTime(Math.max(120, cutoff * 0.25), t + dur);
    const g = ctx.createGain();
    g.gain.setValueAtTime(gain, t);
    g.gain.exponentialRampToValueAtTime(0.001, t + dur);
    src.connect(filter).connect(g).connect(dest);
    src.start(t);
    src.stop(t + dur + 0.02);
  }

  private thump(dest: AudioNode, t: number, freq: number, dur: number, gain: number): void {
    const ctx = this.ctx!;
    const osc = ctx.createOscillator();
    osc.type = 'sine';
    osc.frequency.setValueAtTime(freq, t);
    osc.frequency.exponentialRampToValueAtTime(Math.max(35, freq * 0.55), t + dur);
    const g = ctx.createGain();
    g.gain.setValueAtTime(gain, t);
    g.gain.exponentialRampToValueAtTime(0.001, t + dur);
    osc.connect(g).connect(dest);
    osc.start(t);
    osc.stop(t + dur + 0.02);
  }

  private blip(dest: AudioNode, t: number, freq: number, dur: number, gain: number, type: OscillatorType): void {
    const ctx = this.ctx!;
    const osc = ctx.createOscillator();
    osc.type = type;
    osc.frequency.value = freq;
    const g = ctx.createGain();
    g.gain.setValueAtTime(gain, t);
    g.gain.exponentialRampToValueAtTime(0.001, t + dur);
    osc.connect(g).connect(dest);
    osc.start(t);
    osc.stop(t + dur + 0.02);
  }
}
