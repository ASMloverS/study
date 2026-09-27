import * as THREE from 'three';

export interface SceneCtx {
  scene: THREE.Scene;
  camera: THREE.PerspectiveCamera;
  renderer: THREE.WebGLRenderer;
}

export function createScene(canvasHost: HTMLElement): SceneCtx {
  const renderer = new THREE.WebGLRenderer({ antialias: true });
  renderer.setSize(window.innerWidth, window.innerHeight);
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  canvasHost.appendChild(renderer.domElement);
  const scene = new THREE.Scene();
  scene.background = new THREE.Color(0x9fc7e8);
  scene.fog = new THREE.Fog(0x9fc7e8, 1500, 2800);
  const camera = new THREE.PerspectiveCamera(65, window.innerWidth / window.innerHeight, 0.1, 3000);
  scene.add(new THREE.HemisphereLight(0xcfe8ff, 0x5a7a4a, 0.9));
  const sun = new THREE.DirectionalLight(0xfff3d6, 1.4);
  sun.position.set(300, 500, 200);
  scene.add(sun);
  window.addEventListener('resize', () => {
    camera.aspect = window.innerWidth / window.innerHeight;
    camera.updateProjectionMatrix();
    renderer.setSize(window.innerWidth, window.innerHeight);
  });
  return { scene, camera, renderer };
}
