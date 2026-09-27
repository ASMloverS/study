// @vitest-environment jsdom
import { describe, expect, it, beforeEach } from 'vitest';
import { InputController } from './input';

function press(key: string, repeat = false) {
  window.dispatchEvent(new KeyboardEvent('keydown', { key, bubbles: true, repeat }));
}
function release(key: string) {
  window.dispatchEvent(new KeyboardEvent('keyup', { key, bubbles: true }));
}

describe('InputController', () => {
  let c: InputController;
  let t = 0;
  beforeEach(() => {
    t = 0;
    c = new InputController(() => t);
    c.attach();
  });

  it('W queues +5 cadence delta once', () => {
    press('ArrowUp');
    expect(c.command().cadDelta).toBe(5);
    expect(c.command().cadDelta).toBe(0);
  });
  it('presses accumulate between ticks', () => {
    press('ArrowUp');
    press('W');
    expect(c.command().cadDelta).toBe(10);
  });
  it('S queues -5 cadence delta', () => {
    press('ArrowDown');
    expect(c.command().cadDelta).toBe(-5);
  });
  it('E queues -1 cog delta once', () => {
    press('e');
    expect(c.command().cogDelta).toBe(-1);
    expect(c.command().cogDelta).toBe(0);
  });
  it('Q queues +1 cog delta', () => {
    press('q');
    expect(c.command().cogDelta).toBe(1);
  });
  it('holding Q integrates 6T/s on top of the pulse', () => {
    press('q');
    t += 0.05;
    expect(c.command().cogDelta).toBeCloseTo(1 + 6 * 0.05, 5);
    t += 0.05;
    expect(c.command().cogDelta).toBeCloseTo(6 * 0.05, 5);
  });
  it('holding W integrates 30rpm/s', () => {
    press('w');
    t += 0.05;
    expect(c.command().cadDelta).toBeCloseTo(5 + 30 * 0.05, 5);
    release('w');
    t += 0.05;
    expect(c.command().cadDelta).toBe(0);
  });
  it('caps integration dt at 100ms', () => {
    press('q');
    t += 10;
    expect(c.command().cogDelta).toBeCloseTo(1 + 6 * 0.1, 5);
  });
  it('ignores auto-repeat', () => {
    press('ArrowUp');
    press('ArrowUp', true);
    expect(c.command().cadDelta).toBe(5);
  });
  it('number keys set power gear and leave deltas untouched', () => {
    press('4');
    expect(c.command().gear).toBe(3);
    expect(c.command().cadDelta).toBe(0);
    expect(c.command().cogDelta).toBe(0);
  });
  it('steer holds while key held', () => {
    press('ArrowLeft');
    expect(c.command().steer).toBe(-1);
    release('ArrowLeft');
    expect(c.command().steer).toBe(0);
  });
  it('clamps negative clock drift to zero integration', () => {
    press('q');
    t += 0.05;
    c.command();
    t -= 0.05;
    expect(c.command().cogDelta).toBe(0);
  });
  it('reset clears held keys and re-baselines the clock', () => {
    press('q');
    c.reset();
    t += 0.05;
    expect(c.command().cogDelta).toBe(0);
    expect(c.command().steer).toBe(0);
  });
  it('opposite keys held together cancel out', () => {
    press('q');
    press('e');
    t += 0.05;
    expect(c.command().cogDelta).toBeCloseTo(0, 5);
  });
  it('alias keys on the same axis share one hold rate', () => {
    press('w');
    press('ArrowUp');
    t += 0.05;
    expect(c.command().cadDelta).toBeCloseTo(10 + 30 * 0.05, 5);
  });
});
