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
  beforeEach(() => {
    c = new InputController();
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
  it('ignores auto-repeat', () => {
    press('ArrowUp');
    press('ArrowUp', true);
    expect(c.command().cadDelta).toBe(5);
  });
  it('number keys set power gear and leave cadence untouched', () => {
    press('4');
    expect(c.command().gear).toBe(3);
    expect(c.command().cadDelta).toBe(0);
  });
  it('steer holds while key held', () => {
    press('ArrowLeft');
    expect(c.command().steer).toBe(-1);
    release('ArrowLeft');
    expect(c.command().steer).toBe(0);
  });
});
