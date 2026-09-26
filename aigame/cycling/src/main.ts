import './style.css';
import { Game } from './game';
import { Hud } from './ui/hud';
import { InputController } from './input';

const hud = new Hud();
const input = new InputController();
const game = new Game(hud, input);
document.querySelector('#btn-start')!.addEventListener('click', () => {
  document.querySelector<HTMLElement>('#overlay-start')!.style.display = 'none';
  input.attach();
  game.start();
});
