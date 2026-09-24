import * as THREE from 'three';
import { EffectComposer } from 'three/addons/postprocessing/EffectComposer.js';
import { RenderPass } from 'three/addons/postprocessing/RenderPass.js';
import { UnrealBloomPass } from 'three/addons/postprocessing/UnrealBloomPass.js';
import { SSAOPass } from 'three/addons/postprocessing/SSAOPass.js';
import { OutputPass } from 'three/addons/postprocessing/OutputPass.js';
import { asphaltTexture } from './textures';

export type QualityTier = 'low' | 'medium' | 'high';

export interface SceneCtx {
  scene: THREE.Scene;
  camera: THREE.PerspectiveCamera;
  renderer: THREE.WebGLRenderer;
  sun: THREE.DirectionalLight;
  composer: EffectComposer | null;
  setQuality: (q: QualityTier) => void;
}

const SKY_VERT = `
varying vec3 vDir;
void main() {
  vDir = position;
  vec4 mvPosition = modelViewMatrix * vec4(position, 1.0);
  gl_Position = projectionMatrix * mvPosition;
}`;

const SKY_FRAG = `
varying vec3 vDir;
uniform vec3 sunDir;
void main() {
  vec3 d = normalize(vDir);
  float h = clamp(d.y, -0.05, 1.0);
  vec3 zenith = vec3(0.28, 0.5, 0.82);
  vec3 horizon = vec3(0.82, 0.88, 0.93);
  vec3 col = mix(horizon, zenith, pow(max(h, 0.0), 0.55));
  float sunAmt = max(dot(d, normalize(sunDir)), 0.0);
  col += vec3(1.0, 0.92, 0.75) * pow(sunAmt, 600.0) * 3.0;
  col += vec3(1.0, 0.88, 0.62) * pow(sunAmt, 8.0) * 0.14;
  gl_FragColor = vec4(col, 1.0);
}`;

function buildSky(): THREE.Mesh {
  const mat = new THREE.ShaderMaterial({
    vertexShader: SKY_VERT,
    fragmentShader: SKY_FRAG,
    uniforms: { sunDir: { value: new THREE.Vector3(0.55, 0.72, 0.42).normalize() } },
    side: THREE.BackSide,
    depthWrite: false,
    fog: false,
  });
  const sky = new THREE.Mesh(new THREE.SphereGeometry(240, 24, 16), mat);
  sky.frustumCulled = false;
  return sky;
}

export function createScene(canvas: HTMLCanvasElement): SceneCtx {
  const scene = new THREE.Scene();
  scene.fog = new THREE.FogExp2(0xcfdce6, 0.0045);

  const camera = new THREE.PerspectiveCamera(75, window.innerWidth / window.innerHeight, 0.05, 400);
  camera.rotation.order = 'YXZ';

  const renderer = new THREE.WebGLRenderer({ canvas, antialias: true });
  renderer.setSize(window.innerWidth, window.innerHeight);
  renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
  renderer.shadowMap.enabled = true;
  renderer.shadowMap.type = THREE.PCFSoftShadowMap;
  renderer.toneMapping = THREE.ACESFilmicToneMapping;
  renderer.toneMappingExposure = 1.02;

  const sky = buildSky();
  scene.add(sky);

  const pmrem = new THREE.PMREMGenerator(renderer);
  const envScene = new THREE.Scene();
  envScene.add(buildSky());
  scene.environment = pmrem.fromScene(envScene, 0.02).texture;
  scene.environmentIntensity = 0.7;
  pmrem.dispose();

  const hemi = new THREE.HemisphereLight(0xbfd4e6, 0x4a4f52, 0.42);
  scene.add(hemi);

  const sun = new THREE.DirectionalLight(0xfff1d6, 2.2);
  sun.position.set(33, 43, 25);
  sun.castShadow = true;
  sun.shadow.mapSize.set(2048, 2048);
  sun.shadow.camera.left = -34;
  sun.shadow.camera.right = 34;
  sun.shadow.camera.top = 34;
  sun.shadow.camera.bottom = -34;
  sun.shadow.camera.far = 140;
  sun.shadow.bias = -0.0004;
  scene.add(sun);

  const groundTex = asphaltTexture(256, 14);
  const ground = new THREE.Mesh(
    new THREE.PlaneGeometry(140, 140),
    new THREE.MeshStandardMaterial({
      map: groundTex.map,
      normalMap: groundTex.normalMap,
      roughnessMap: groundTex.roughnessMap,
      roughness: 1,
      metalness: 0.02,
      normalScale: new THREE.Vector2(0.8, 0.8),
    }),
  );
  ground.rotation.x = -Math.PI / 2;
  ground.receiveShadow = true;
  scene.add(ground);

  const composer = new EffectComposer(renderer);
  composer.addPass(new RenderPass(scene, camera));
  const ssao = new SSAOPass(scene, camera, window.innerWidth, window.innerHeight);
  ssao.kernelRadius = 0.5;
  ssao.minDistance = 0.002;
  ssao.maxDistance = 0.12;
  composer.addPass(ssao);
  const bloom = new UnrealBloomPass(new THREE.Vector2(window.innerWidth, window.innerHeight), 0.32, 0.55, 0.86);
  composer.addPass(bloom);
  composer.addPass(new OutputPass());

  const ctx: SceneCtx = {
    scene,
    camera,
    renderer,
    sun,
    composer,
    setQuality: (q: QualityTier) => {
      ssao.enabled = q === 'high';
      bloom.enabled = q !== 'low';
      ctx.composer = q === 'low' ? null : composer;
    },
  };
  ctx.setQuality('high');

  window.addEventListener('resize', () => {
    camera.aspect = window.innerWidth / window.innerHeight;
    camera.updateProjectionMatrix();
    renderer.setSize(window.innerWidth, window.innerHeight);
    composer.setSize(window.innerWidth, window.innerHeight);
  });

  return ctx;
}
