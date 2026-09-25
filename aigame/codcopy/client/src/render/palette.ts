import * as THREE from 'three';

/** [M13] 卡通渲染共享资源：3 阶 gradientMap + 工业波普色板 + toon 材质工厂（全场景单一来源）。 */

export const GRADIENT_MAP = new THREE.DataTexture(new Uint8Array([110, 185, 255]), 3, 1, THREE.RedFormat);
GRADIENT_MAP.minFilter = THREE.NearestFilter;
GRADIENT_MAP.magFilter = THREE.NearestFilter;
GRADIENT_MAP.generateMipmaps = false;
GRADIENT_MAP.needsUpdate = true;

/** 工业波普色板（地面 / 地图掩体与装饰；角色 / 武器 / 特效色见各自模块） */
export const PALETTE = {
  ground: 0xd8dee6,
  wall: 0xcdd6df,
  lowWall: 0xb4bfca,
  containerOrange: 0xff7a1a,
  containerBlue: 0x1f8fff,
  crate: 0xe09a3c,
  barrel: 0xff4030,
  band: 0xffd60a,
  platform: 0x54c8d8,
  metal: 0x9fb0bc,
} as const;

export function toonMat(color: number): THREE.MeshToonMaterial {
  return new THREE.MeshToonMaterial({ color, gradientMap: GRADIENT_MAP });
}
