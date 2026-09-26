import { RACE } from '../sim/params';

export interface FramePlan {
  steps: number;
  acc: number;
  alpha: number;
}

export function planFrame(acc: number, frameDt: number, dt: number = RACE.dt): FramePlan {
  const total = Math.min(acc + frameDt, 0.25);
  const steps = Math.floor(total / dt);
  const rest = total - steps * dt;
  return { steps, acc: rest, alpha: rest / dt };
}

export function createLoop(update: (dt: number) => void, render: (alpha: number, frameDt: number) => void) {
  let raf = 0;
  let last = 0;
  let acc = 0;
  const tick = (now: number) => {
    const frameDt = Math.min((now - last) / 1000, 0.25);
    last = now;
    const plan = planFrame(acc, frameDt);
    acc = plan.acc;
    for (let i = 0; i < plan.steps; i++) update(RACE.dt);
    render(plan.alpha, frameDt);
    raf = requestAnimationFrame(tick);
  };
  return {
    start() {
      last = performance.now();
      raf = requestAnimationFrame(tick);
    },
    stop() {
      cancelAnimationFrame(raf);
    },
  };
}
