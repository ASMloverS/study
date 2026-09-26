import './style.css';
import { Game } from './game';
import { Hud } from './ui/hud';
import { InputController } from './input';
import { Music } from './audio';

const hud = new Hud();
const input = new InputController();
const music = new Music();
const game = new Game(hud, input, music);
window.addEventListener('keydown', (e) => {
  if (e.key.toLowerCase() === 'm') music.toggle();
});
document.querySelector('#btn-start')!.addEventListener('click', () => {
  document.querySelector<HTMLElement>('#overlay-start')!.style.display = 'none';
  input.attach();
  music.start();
  game.start();
});
