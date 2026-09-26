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

  it('gear up on ArrowUp', () => {
    press('ArrowUp');
    expect(c.command().gear).toBe(2);
  });
  it('ignores auto-repeat', () => {
    press('ArrowUp');
    press('ArrowUp', true);
    expect(c.command().gear).toBe(2);
  });
  it('gear down clamps at 0', () => {
    press('ArrowDown');
    press('ArrowDown');
    press('ArrowDown');
    expect(c.command().gear).toBe(0);
  });
  it('direct gear select with number keys', () => {
    press('4');
    expect(c.command().gear).toBe(3);
  });
  it('steer holds while key held', () => {
    press('ArrowLeft');
    expect(c.command().steer).toBe(-1);
    release('ArrowLeft');
    expect(c.command().steer).toBe(0);
  });
});
