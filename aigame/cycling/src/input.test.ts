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

  it('shifts cassette heavier on ArrowUp', () => {
    press('ArrowUp');
    expect(c.command().cog).toBe(7);
  });
  it('shifts cassette lighter on ArrowDown', () => {
    press('ArrowDown');
    expect(c.command().cog).toBe(5);
  });
  it('ignores auto-repeat', () => {
    press('ArrowUp');
    press('ArrowUp', true);
    expect(c.command().cog).toBe(7);
  });
  it('cog clamps to 0..11', () => {
    for (let i = 0; i < 20; i++) press('ArrowDown');
    expect(c.command().cog).toBe(0);
    for (let i = 0; i < 20; i++) press('ArrowUp');
    expect(c.command().cog).toBe(11);
  });
  it('number keys set power gear', () => {
    press('4');
    expect(c.command().gear).toBe(3);
    expect(c.command().cog).toBe(6);
  });
  it('steer holds while key held', () => {
    press('ArrowLeft');
    expect(c.command().steer).toBe(-1);
    release('ArrowLeft');
    expect(c.command().steer).toBe(0);
  });
});
