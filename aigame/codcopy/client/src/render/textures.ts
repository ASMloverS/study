import * as THREE from 'three';

/** 程序化 canvas 纹理工厂（M7.2）：漫反射 + 法线（高度图 Sobel）+ 粗糙度，零外部资源。 */

export interface TextureSet {
  map: THREE.CanvasTexture;
  normalMap: THREE.CanvasTexture;
  roughnessMap: THREE.CanvasTexture;
}

function canvas(size: number): [HTMLCanvasElement, CanvasRenderingContext2D] {
  const c = document.createElement('canvas');
  c.width = size;
  c.height = size;
  return [c, c.getContext('2d')!];
}

function tex(c: HTMLCanvasElement, repeat = 1): THREE.CanvasTexture {
  const t = new THREE.CanvasTexture(c);
  t.wrapS = THREE.RepeatWrapping;
  t.wrapT = THREE.RepeatWrapping;
  t.repeat.set(repeat, repeat);
  t.colorSpace = THREE.SRGBColorSpace;
  t.anisotropy = 4;
  return t;
}

function normalTex(c: HTMLCanvasElement, repeat = 1): THREE.CanvasTexture {
  const t = new THREE.CanvasTexture(c);
  t.wrapS = THREE.RepeatWrapping;
  t.wrapT = THREE.RepeatWrapping;
  t.repeat.set(repeat, repeat);
  return t;
}

/** 高度图 → 法线图（Sobel） */
function heightToNormal(height: Float32Array, size: number, strength = 2): HTMLCanvasElement {
  const [c, ctx] = canvas(size);
  const img = ctx.createImageData(size, size);
  const at = (x: number, y: number): number => height[((y + size) % size) * size + ((x + size) % size)];
  for (let y = 0; y < size; y++) {
    for (let x = 0; x < size; x++) {
      const dx = (at(x + 1, y) - at(x - 1, y)) * strength;
      const dy = (at(x, y + 1) - at(x, y - 1)) * strength;
      const len = Math.hypot(dx, dy, 1);
      const i = (y * size + x) * 4;
      img.data[i] = ((-dx / len) * 0.5 + 0.5) * 255;
      img.data[i + 1] = ((-dy / len) * 0.5 + 0.5) * 255;
      img.data[i + 2] = (1 / len) * 0.5 * 255 + 127;
      img.data[i + 3] = 255;
    }
  }
  ctx.putImageData(img, 0, 0);
  return c;
}

function valueNoise(size: number, cells: number, rng: () => number): Float32Array {
  const grid = new Float32Array((cells + 1) * (cells + 1));
  for (let i = 0; i < grid.length; i++) grid[i] = rng();
  const out = new Float32Array(size * size);
  const smooth = (t: number): number => t * t * (3 - 2 * t);
  for (let y = 0; y < size; y++) {
    const gy = (y / size) * cells;
    const y0 = Math.floor(gy);
    const fy = smooth(gy - y0);
    for (let x = 0; x < size; x++) {
      const gx = (x / size) * cells;
      const x0 = Math.floor(gx);
      const fx = smooth(gx - x0);
      const a = grid[y0 * (cells + 1) + x0];
      const b = grid[y0 * (cells + 1) + x0 + 1];
      const c = grid[(y0 + 1) * (cells + 1) + x0];
      const d = grid[(y0 + 1) * (cells + 1) + x0 + 1];
      out[y * size + x] = a + (b - a) * fx + (c - a) * fy + (a - b - c + d) * fx * fy;
    }
  }
  return out;
}

function mulberry(seed: number): () => number {
  let a = seed >>> 0;
  return () => {
    a |= 0;
    a = (a + 0x6d2b79f5) | 0;
    let t = Math.imul(a ^ (a >>> 15), 1 | a);
    t = (t + Math.imul(t ^ (t >>> 7), 61 | t)) ^ t;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };
}

/** 混凝土面板：接缝 + 噪点 + 裂纹 + 底部污渍 */
export function concreteTexture(size = 256, repeat = 1): TextureSet {
  const rng = mulberry(101);
  const n = valueNoise(size, 32, rng);
  const n2 = valueNoise(size, 128, rng);
  const [c, ctx] = canvas(size);
  const img = ctx.createImageData(size, size);
  const height = new Float32Array(size * size);
  for (let i = 0; i < size * size; i++) {
    const x = i % size;
    const y = (i / size) | 0;
    let v = 0.72 + (n[i] - 0.5) * 0.12 + (n2[i] - 0.5) * 0.08;
    const seam = Math.min(x % (size / 2), y % (size / 2), size / 2 - (x % (size / 2)), size / 2 - (y % (size / 2)));
    if (seam < 2) v -= 0.28;
    if (y > size * 0.72) v -= ((y / size - 0.72) / 0.28) * 0.16;
    const crack = Math.abs(n2[i] - 0.5) < 0.012 && n[i] > 0.5 ? -0.2 : 0;
    v += crack;
    height[i] = v;
    const g = Math.max(0, Math.min(1, v)) * 255;
    img.data[i * 4] = g * 1.02;
    img.data[i * 4 + 1] = g;
    img.data[i * 4 + 2] = g * 0.96;
    img.data[i * 4 + 3] = 255;
  }
  ctx.putImageData(img, 0, 0);
  for (let i = 0; i < 24; i++) {
    ctx.fillStyle = `rgba(40,42,40,${0.04 + rng() * 0.06})`;
    ctx.beginPath();
    ctx.arc(rng() * size, rng() * size, 4 + rng() * 18, 0, Math.PI * 2);
    ctx.fill();
  }
  return { map: tex(c, repeat), normalMap: normalTex(heightToNormal(height, size, 1.6), repeat), roughnessMap: roughFromHeight(height, size, repeat) };
}

function roughFromHeight(height: Float32Array, size: number, repeat: number): THREE.CanvasTexture {
  const [c, ctx] = canvas(size);
  const img = ctx.createImageData(size, size);
  for (let i = 0; i < size * size; i++) {
    const g = Math.max(0, Math.min(1, 0.82 - (height[i] - 0.7) * 0.4)) * 255;
    img.data[i * 4] = g;
    img.data[i * 4 + 1] = g;
    img.data[i * 4 + 2] = g;
    img.data[i * 4 + 3] = 255;
  }
  ctx.putImageData(img, 0, 0);
  return normalTex(c, repeat);
}

/** 沥青地面：颗粒 + 轮胎痕 + 油污 */
export function asphaltTexture(size = 256, repeat = 1): TextureSet {
  const rng = mulberry(202);
  const n = valueNoise(size, 64, rng);
  const n2 = valueNoise(size, 8, rng);
  const [c, ctx] = canvas(size);
  const img = ctx.createImageData(size, size);
  const height = new Float32Array(size * size);
  for (let i = 0; i < size * size; i++) {
    const v = 0.5 + (n[i] - 0.5) * 0.35 + (n2[i] - 0.5) * 0.06;
    height[i] = v;
    const g = Math.max(0, Math.min(1, v)) * 255;
    img.data[i * 4] = g;
    img.data[i * 4 + 1] = g;
    img.data[i * 4 + 2] = g * 1.02;
    img.data[i * 4 + 3] = 255;
  }
  ctx.putImageData(img, 0, 0);
  for (let i = 0; i < 5; i++) {
    const x = rng() * size;
    ctx.strokeStyle = `rgba(20,20,22,${0.15 + rng() * 0.15})`;
    ctx.lineWidth = 6 + rng() * 10;
    ctx.beginPath();
    ctx.moveTo(x, 0);
    ctx.bezierCurveTo(x + 20 - rng() * 40, size / 3, x + 20 - rng() * 40, (size * 2) / 3, x + 10 - rng() * 20, size);
    ctx.stroke();
  }
  for (let i = 0; i < 6; i++) {
    const grd = ctx.createRadialGradient(rng() * size, rng() * size, 2, rng() * size, rng() * size, 14 + rng() * 22);
    grd.addColorStop(0, 'rgba(14,12,10,0.5)');
    grd.addColorStop(1, 'rgba(14,12,10,0)');
    ctx.fillStyle = grd;
    ctx.fillRect(0, 0, size, size);
  }
  return { map: tex(c, repeat), normalMap: normalTex(heightToNormal(height, size, 2.2), repeat), roughnessMap: roughFromHeight(height, size, repeat) };
}

/** 集装箱波纹钢板：竖向波纹 + 锈迹 + 模版字 */
export function corrugatedTexture(size = 256, repeat = 1, base = [176, 88, 44], text = 'CARGO 42'): TextureSet {
  const rng = mulberry(303);
  const n = valueNoise(size, 48, rng);
  const [c, ctx] = canvas(size);
  const height = new Float32Array(size * size);
  const ribs = 8;
  const img = ctx.createImageData(size, size);
  for (let i = 0; i < size * size; i++) {
    const x = i % size;
    const rib = x / (size / ribs) % 1;
    const wave = Math.abs(rib - 0.5) * 2;
    const v = wave * wave * 0.4 + 0.6 + (n[i] - 0.5) * 0.15;
    height[i] = v;
    const rust = Math.max(0, n[i] - 0.62) * (1 - wave * 0.5);
    const r = base[0] * v * (1 - rust) + 120 * rust;
    const g = base[1] * v * (1 - rust) + 62 * rust;
    const b = base[2] * v * (1 - rust) + 30 * rust;
    img.data[i * 4] = Math.min(255, r);
    img.data[i * 4 + 1] = Math.min(255, g);
    img.data[i * 4 + 2] = Math.min(255, b);
    img.data[i * 4 + 3] = 255;
  }
  ctx.putImageData(img, 0, 0);
  ctx.fillStyle = 'rgba(230,230,225,0.85)';
  ctx.font = `bold ${size / 9}px monospace`;
  ctx.fillText(text, size * 0.08, size * 0.5);
  ctx.fillText(text, size * 0.08, size * 0.86);
  for (let i = 0; i < 10; i++) {
    ctx.fillStyle = `rgba(90,50,25,${0.1 + rng() * 0.15})`;
    ctx.fillRect(rng() * size, size * 0.55 + rng() * size * 0.4, 2 + rng() * 4, 8 + rng() * 26);
  }
  return { map: tex(c, repeat), normalMap: normalTex(heightToNormal(height, size, 3.2), repeat), roughnessMap: roughFromHeight(height, size, repeat) };
}

/** 木板条：纹理 + 钉子 + 板缝 */
export function woodTexture(size = 256, repeat = 1): TextureSet {
  const rng = mulberry(404);
  const n = valueNoise(size, 16, rng);
  const n2 = valueNoise(size, 96, rng);
  const [c, ctx] = canvas(size);
  const img = ctx.createImageData(size, size);
  const height = new Float32Array(size * size);
  const planks = 4;
  for (let i = 0; i < size * size; i++) {
    const x = i % size;
    const y = (i / size) | 0;
    const grain = Math.sin(x * 0.7 + n[i] * 9) * 0.06;
    const plank = Math.floor((y / size) * planks);
    const gap = y % (size / planks) < 2 ? -0.25 : 0;
    const v = 0.68 + grain + (n2[i] - 0.5) * 0.1 + gap + (plank % 2 === 0 ? 0.04 : -0.02);
    height[i] = v;
    img.data[i * 4] = Math.min(255, 175 * v);
    img.data[i * 4 + 1] = Math.min(255, 128 * v);
    img.data[i * 4 + 2] = Math.min(255, 82 * v);
    img.data[i * 4 + 3] = 255;
  }
  ctx.putImageData(img, 0, 0);
  for (let p = 0; p < planks; p++) {
    const y = (p + 0.5) * (size / planks);
    for (const xx of [size * 0.12, size * 0.88]) {
      ctx.fillStyle = 'rgba(40,38,36,0.8)';
      ctx.beginPath();
      ctx.arc(xx, y, 2.4, 0, Math.PI * 2);
      ctx.fill();
    }
  }
  return { map: tex(c, repeat), normalMap: normalTex(heightToNormal(height, size, 1.8), repeat), roughnessMap: roughFromHeight(height, size, repeat) };
}

/** 警示黄黑斜条纹（滑门 / 油桶环带） */
export function cautionTexture(size = 128, repeat = 1): TextureSet {
  const [c, ctx] = canvas(size);
  ctx.fillStyle = '#d8a520';
  ctx.fillRect(0, 0, size, size);
  ctx.fillStyle = '#191919';
  for (let i = -size; i < size * 2; i += size / 4) {
    ctx.beginPath();
    ctx.moveTo(i, 0);
    ctx.lineTo(i + size / 8, 0);
    ctx.lineTo(i + size / 8 + size, size);
    ctx.lineTo(i + size, size);
    ctx.closePath();
    ctx.fill();
  }
  ctx.globalAlpha = 0.12;
  const rng = mulberry(505);
  for (let i = 0; i < 300; i++) {
    ctx.fillStyle = rng() > 0.5 ? '#fff' : '#000';
    ctx.fillRect(rng() * size, rng() * size, 2, 2);
  }
  const t = tex(c, repeat);
  return { map: t, normalMap: t, roughnessMap: t };
}

/** 锈蚀金属（油桶体） */
export function rustTexture(size = 256, repeat = 1): TextureSet {
  const rng = mulberry(606);
  const n = valueNoise(size, 24, rng);
  const n2 = valueNoise(size, 96, rng);
  const [c, ctx] = canvas(size);
  const img = ctx.createImageData(size, size);
  const height = new Float32Array(size * size);
  for (let i = 0; i < size * size; i++) {
    const rust = Math.max(0, Math.min(1, (n[i] - 0.35) * 1.6 + (n2[i] - 0.5) * 0.3));
    height[i] = 0.7 - rust * 0.3 + (n2[i] - 0.5) * 0.1;
    img.data[i * 4] = 120 - rust * 40 + n2[i] * 30;
    img.data[i * 4 + 1] = 80 - rust * 40 + n2[i] * 20;
    img.data[i * 4 + 2] = 58 - rust * 30 + n2[i] * 12;
    img.data[i * 4 + 3] = 255;
  }
  ctx.putImageData(img, 0, 0);
  return { map: tex(c, repeat), normalMap: normalTex(heightToNormal(height, size, 2), repeat), roughnessMap: roughFromHeight(height, size, repeat) };
}
