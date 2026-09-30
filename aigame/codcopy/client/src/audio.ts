export type SoundKind =
  | 'ar_shot'
  | 'sg_shot'
  | 'sr_shot'
  | 'smg_shot'
  | 'lmg_shot'
  | 'dmr_shot'
  | 'pistol_shot'
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

/** [M15] 五层枪声参数：crack 高频裂响 / body 中频主体 / sub 低频砰 / tail 混响湿度 / mech 枪机声 */
export interface ShotParams {
  crackHz: number;
  crackDur: number;
  crackGain: number;
  bodyHz: number;
  bodyDur: number;
  bodyGain: number;
  subHz: number;
  subDur: number;
  subGain: number;
  tailGain: number;
  mechGain: number;
  mechHz: number;
}

export const SHOT_PARAMS: Record<'ar_shot' | 'smg_shot' | 'lmg_shot' | 'dmr_shot' | 'sg_shot' | 'sr_shot' | 'pistol_shot', ShotParams> = {
  ar_shot:     { crackHz: 4200, crackDur: 0.05,  crackGain: 0.3,  bodyHz: 900,  bodyDur: 0.14, bodyGain: 0.28, subHz: 100, subDur: 0.06,  subGain: 0.25, tailGain: 0.5,  mechGain: 0.06, mechHz: 2800 },
  smg_shot:    { crackHz: 5200, crackDur: 0.035, crackGain: 0.24, bodyHz: 1100, bodyDur: 0.09, bodyGain: 0.2,  subHz: 130, subDur: 0.04,  subGain: 0.16, tailGain: 0.3,  mechGain: 0.05, mechHz: 3200 },
  lmg_shot:    { crackHz: 3200, crackDur: 0.07,  crackGain: 0.34, bodyHz: 650,  bodyDur: 0.2,  bodyGain: 0.38, subHz: 75,  subDur: 0.1,   subGain: 0.34, tailGain: 0.8,  mechGain: 0.12, mechHz: 1800 },
  dmr_shot:    { crackHz: 3800, crackDur: 0.06,  crackGain: 0.34, bodyHz: 700,  bodyDur: 0.16, bodyGain: 0.32, subHz: 85,  subDur: 0.09,  subGain: 0.3,  tailGain: 0.7,  mechGain: 0.1,  mechHz: 2400 },
  sg_shot:     { crackHz: 2400, crackDur: 0.09,  crackGain: 0.4,  bodyHz: 450,  bodyDur: 0.26, bodyGain: 0.42, subHz: 55,  subDur: 0.14,  subGain: 0.4,  tailGain: 0.9,  mechGain: 0.08, mechHz: 1600 },
  sr_shot:     { crackHz: 3000, crackDur: 0.1,   crackGain: 0.42, bodyHz: 500,  bodyDur: 0.3,  bodyGain: 0.44, subHz: 60,  subDur: 0.16,  subGain: 0.42, tailGain: 1,    mechGain: 0.1,  mechHz: 2000 },
  pistol_shot: { crackHz: 4600, crackDur: 0.04,  crackGain: 0.22, bodyHz: 1000, bodyDur: 0.08, bodyGain: 0.18, subHz: 150, subDur: 0.035, subGain: 0.15, tailGain: 0.25, mechGain: 0.07, mechHz: 3000 },
};

/** [M15] 远距枪声低通：越远越闷，钳制 [500, 8000] */
export function distanceCutoff(dist: number): number {
  return Math.max(500, Math.min(8000, 8000 - dist * 90));
}

/** [M17] 爆炸声专属远距低通：低频传得远，下限 240Hz */
export function explodeCutoff(dist: number): number {
  return Math.max(240, Math.min(8000, 8000 - dist * 95));
}

/** [M17] 连杀余烬声节流：冷却窗口内只放行一轮燃烧声（防 5-8 枚落弹各触发一份） */
export function burnWindowOpen(lastBurnAt: number, now: number, cooldownMs = 5000): boolean {
  return now - lastBurnAt >= cooldownMs;
}

export class AudioSys {
  private ctx: AudioContext | null = null;
  private master: GainNode | null = null;
  private noise: AudioBuffer | null = null;
  private volume = 0.7;
  private ttsOn = true;
  private compressor: DynamicsCompressorNode | null = null;
  private convolver: ConvolverNode | null = null;
  private tailBus: GainNode | null = null;
  private listenerPos = { x: 0, y: 0, z: 0 };

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
      this.compressor = this.ctx.createDynamicsCompressor();
      this.compressor.threshold.value = -12;
      this.compressor.ratio.value = 6;
      this.master.connect(this.compressor).connect(this.ctx.destination);
      // [M15] 合成混响 IR：1.8s 指数衰减白噪（枪声 tail 层专用）
      const irLen = Math.floor(this.ctx.sampleRate * 1.8);
      const ir = this.ctx.createBuffer(2, irLen, this.ctx.sampleRate);
      for (let ch = 0; ch < 2; ch++) {
        const d = ir.getChannelData(ch);
        for (let i = 0; i < irLen; i++) d[i] = (Math.random() * 2 - 1) * Math.pow(1 - i / irLen, 2.6);
      }
      this.convolver = this.ctx.createConvolver();
      this.convolver.buffer = ir;
      this.tailBus = this.ctx.createGain();
      this.tailBus.gain.value = 0.32;
      this.tailBus.connect(this.convolver).connect(this.master);
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
    this.listenerPos = { x, y, z };
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

  playAt(kind: SoundKind, x: number, y: number, z: number, mag = 1): void {
    if (!this.ctx || !this.master) return;
    const panner = this.ctx.createPanner();
    panner.panningModel = 'equalpower';
    panner.distanceModel = 'inverse';
    const boom = kind === 'explode';
    panner.refDistance = boom ? 10 : 4;
    panner.maxDistance = boom ? 200 : 80;
    panner.rolloffFactor = boom ? 1.0 : 1.4;
    panner.positionX.value = x;
    panner.positionY.value = y;
    panner.positionZ.value = z;
    const dist = Math.hypot(x - this.listenerPos.x, y - this.listenerPos.y, z - this.listenerPos.z);
    const lp = this.ctx.createBiquadFilter();
    lp.type = 'lowpass';
    lp.frequency.value = boom ? explodeCutoff(dist) : distanceCutoff(dist);
    panner.connect(lp).connect(this.master);
    this.emit(kind, panner, mag);
  }

  /** [M15] 喷气机呼啸掠过（空袭/集束 incoming，所有客户端 3D 可闻） */
  jetFlyby(x: number, z: number, yaw: number): void {
    if (!this.ctx || !this.master) return;
    const ctx = this.ctx;
    const t = ctx.currentTime;
    const dx = -Math.sin(yaw);
    const dz = -Math.cos(yaw);
    const panner = ctx.createPanner();
    panner.panningModel = 'equalpower';
    panner.distanceModel = 'inverse';
    panner.refDistance = 12;
    panner.maxDistance = 160;
    panner.rolloffFactor = 0.9;
    panner.positionY.value = 8;
    panner.positionX.setValueAtTime(x - dx * 40, t);
    panner.positionZ.setValueAtTime(z - dz * 40, t);
    panner.positionX.linearRampToValueAtTime(x + dx * 40, t + 1.6);
    panner.positionZ.linearRampToValueAtTime(z + dz * 40, t + 1.6);
    panner.connect(this.master);
    const src = ctx.createBufferSource();
    src.buffer = this.noise;
    src.loop = true;
    const bp = ctx.createBiquadFilter();
    bp.type = 'bandpass';
    bp.Q.value = 1.1;
    bp.frequency.setValueAtTime(2400, t);
    bp.frequency.exponentialRampToValueAtTime(420, t + 1.6);
    const g = ctx.createGain();
    g.gain.setValueAtTime(0.0001, t);
    g.gain.exponentialRampToValueAtTime(0.5, t + 0.5);
    g.gain.exponentialRampToValueAtTime(0.0001, t + 1.7);
    src.connect(bp).connect(g).connect(panner);
    src.start(t);
    src.stop(t + 1.75);
    this.thump(panner, t + 0.4, 75, 1.1, 0.28);
  }

  /** [M17] 落弹区持续燃烧轰鸣 + 随机噼啪（3D 定位，durSec 线性渐弱） */
  burningLoop(x: number, z: number, durSec = 7): void {
    if (!this.ctx || !this.master || !this.noise) return;
    const ctx = this.ctx;
    const t = ctx.currentTime;
    const panner = ctx.createPanner();
    panner.panningModel = 'equalpower';
    panner.distanceModel = 'inverse';
    panner.refDistance = 6;
    panner.maxDistance = 60;
    panner.rolloffFactor = 1.2;
    panner.positionX.value = x;
    panner.positionY.value = 0.8;
    panner.positionZ.value = z;
    const lp = ctx.createBiquadFilter();
    lp.type = 'lowpass';
    lp.frequency.value = 180;
    const src = ctx.createBufferSource();
    src.buffer = this.noise;
    src.loop = true;
    const g = ctx.createGain();
    g.gain.setValueAtTime(0.5, t);
    g.gain.linearRampToValueAtTime(0.0001, t + durSec);
    src.connect(lp).connect(g).connect(panner).connect(this.master);
    src.start(t);
    src.stop(t + durSec + 0.05);
    let at = t + 0.4;
    while (at < t + durSec - 0.2) {
      this.snap(panner, at, 1800 + Math.random() * 900, 0.03, 0.15);
      at += 0.3 + Math.random() * 0.4;
    }
  }

  private emit(kind: SoundKind, dest: AudioNode, mag = 1): void {
    const ctx = this.ctx!;
    const t = ctx.currentTime;
    switch (kind) {
      case 'ar_shot':
      case 'smg_shot':
      case 'lmg_shot':
      case 'dmr_shot':
      case 'sg_shot':
      case 'sr_shot':
      case 'pistol_shot': {
        const p = SHOT_PARAMS[kind];
        this.snap(dest, t, p.crackHz, p.crackDur, p.crackGain);
        this.burst(dest, t, p.bodyHz, p.bodyDur, p.bodyGain);
        this.thump(dest, t, p.subHz, p.subDur, p.subGain);
        this.mech(dest, t + 0.01, p.mechHz, p.mechGain);
        if (this.tailBus && dest === this.master) {
          // [M15] 本地枪声送混响 tail（远端经距离低通已够闷）
          const send = this.ctx!.createGain();
          send.gain.value = p.tailGain * 0.28;
          this.burst(send, t, 1400, 0.12 + p.tailGain * 0.15, 0.5);
          send.connect(this.tailBus);
        }
        break;
      }
      case 'hit':
        this.thump(dest, t, 280, 0.05, 0.18);
        this.burst(dest, t, 600, 0.03, 0.08);
        break;
      case 'headshot':
        this.snap(dest, t, 2600, 0.06, 0.32);
        this.thump(dest, t, 180, 0.1, 0.4);
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
      case 'explode': {
        this.snap(dest, t, 2200, 0.05, 0.3 * mag);
        this.burst(dest, t, 850, 0.75, 0.6 * mag);
        this.thump(dest, t, 52, 0.9, 0.55 * mag);
        this.thump(dest, t + 0.05, 90, 0.3, 0.3 * mag);
        if (this.tailBus) {
          // [M17] 轰鸣层送混响总线 → 1.8s 滚雷回响尾
          const send = this.ctx!.createGain();
          send.gain.value = 0.5 * mag;
          this.burst(send, t, 700, 0.6, 0.5);
          send.connect(this.tailBus);
        }
        break;
      }
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

  private snap(dest: AudioNode, t: number, hz: number, dur: number, gain: number): void {
    const ctx = this.ctx!;
    const src = ctx.createBufferSource();
    src.buffer = this.noise;
    src.loop = true;
    const hp = ctx.createBiquadFilter();
    hp.type = 'highpass';
    hp.frequency.value = hz;
    const g = ctx.createGain();
    g.gain.setValueAtTime(gain, t);
    g.gain.exponentialRampToValueAtTime(0.001, t + dur);
    src.connect(hp).connect(g).connect(dest);
    src.start(t);
    src.stop(t + dur + 0.02);
  }

  private mech(dest: AudioNode, t: number, hz: number, gain: number): void {
    this.blip(dest, t, hz, 0.018, gain, 'square');
    this.blip(dest, t + 0.028, hz * 0.72, 0.014, gain * 0.7, 'square');
  }
}
