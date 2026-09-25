import type { PlayerSnap, Standing } from 'shared';

export class Hud {
  private readonly healthbar = el<HTMLDivElement>('healthbar');
  private readonly healthtext = el<HTMLDivElement>('healthtext');
  private readonly ammo = el<HTMLDivElement>('ammo');
  private readonly reloadbar = el<HTMLDivElement>('reloadbar');
  private readonly reloadfill = el<HTMLDivElement>('reloadfill');
  private readonly weaponEl = el<HTMLDivElement>('weapon');
  private readonly score = el<HTMLDivElement>('score');
  private readonly killfeed = el<HTMLUListElement>('killfeed');
  private readonly hitmarker = el<HTMLDivElement>('hitmarker');
  private readonly vignette = el<HTMLDivElement>('vignette');
  private readonly deathoverlay = el<HTMLDivElement>('deathoverlay');
  private readonly deathtext = el<HTMLDivElement>('deathtext');
  private readonly respawncount = el<HTMLDivElement>('respawncount');
  private readonly killmsg = el<HTMLDivElement>('killmsg');
  private readonly scoreboard = el<HTMLDivElement>('scoreboard');
  private readonly scoreboardBody = el<HTMLTableSectionElement>('scorebody');
  private readonly dmgdir = el<HTMLDivElement>('dmgdir');
  private readonly scope = el<HTMLDivElement>('scope');
  private readonly equip = el<HTMLDivElement>('equip');
  private readonly streaksEl = el<HTMLDivElement>('streaks');
  private readonly breathbar = el<HTMLDivElement>('breathbar');
  private readonly breathfill = el<HTMLDivElement>('breathfill');
  private readonly flashoverlay = el<HTMLDivElement>('flashoverlay');
  private flashStart = 0;
  private flashUntil = 0;
  private readonly endoverlay = el<HTMLDivElement>('endoverlay');
  private readonly endtitle = el<HTMLDivElement>('endtitle');
  private readonly endbody = el<HTMLTableSectionElement>('endbody');
  private readonly crosshairArms = Array.from(document.querySelectorAll<HTMLSpanElement>('#crosshair .arm'));
  private deathAt = 0;
  private hitAt = 0;
  private dmgUntil = 0;
  private reloadTotal = 0;
  onRestart: (() => void) | null = null;
  onMenu: (() => void) | null = null;

  constructor() {
    el<HTMLButtonElement>('restartbtn').addEventListener('click', () => this.onRestart?.());
    el<HTMLButtonElement>('menubtn').addEventListener('click', () => this.onMenu?.());
  }

  update(self: PlayerSnap | undefined, timeLeft: number, now: number, ping: number | null = null, killLimit = 30): void {
    if (self) {
      this.healthbar.style.width = `${self.hp}%`;
      this.healthbar.style.background = self.hp > 60 ? '#7ec850' : self.hp > 30 ? '#e0b13e' : '#d84f3f';
      this.healthtext.textContent = String(self.hp);
      this.ammo.textContent = `${self.m} / ${self.rs}`;
      this.weaponEl.textContent = weaponName(self.w);
      if (self.rl > 0) {
        this.reloadTotal = Math.max(this.reloadTotal, self.rl);
        this.reloadbar.style.opacity = '1';
        this.reloadfill.style.width = `${Math.round((1 - self.rl / this.reloadTotal) * 100)}%`;
      } else {
        this.reloadTotal = 0;
        this.reloadbar.style.opacity = '0';
      }
      const pingStr = ping !== null ? ` &nbsp;·&nbsp; ping ${ping}ms` : '';
      this.score.innerHTML = `击杀 ${self.k} / ${killLimit} &nbsp;·&nbsp; 死亡 ${self.d}${pingStr}<br>${fmtTime(timeLeft)}`;
      this.equip.textContent = `手雷 ${self.le ?? 1} · 闪光 ${self.ta ?? 1}`;
      const sv = self.sv ?? 0;
      const parts: string[] = [];
      if (sv & 1) parts.push('UAV[4]');
      if (sv & 2) parts.push('空袭[5]');
      if (sv & 4) parts.push('集束[6]');
      this.streaksEl.textContent = parts.length > 0 ? `连杀奖励就绪：${parts.join(' ')}` : '';
      this.streaksEl.style.color = parts.length > 0 ? '#ffd76a' : '';
    }
    if (this.flashUntil > now) {
      const k = (this.flashUntil - now) / Math.max(1, this.flashUntil - this.flashStart);
      this.flashoverlay.style.opacity = String(Math.min(1, k * 1.6));
    } else {
      this.flashoverlay.style.opacity = '0';
    }
    this.hitmarker.style.opacity = now - this.hitAt < 160 ? '1' : '0';
    this.vignette.style.opacity = self && self.hp < 35 && self.a ? '0.6' : '0';
    if (this.deathAt > 0) {
      const remain = Math.max(0, 3 - (now - this.deathAt) / 1000);
      this.respawncount.textContent = `${remain.toFixed(1)} 秒后重生`;
    }
    if (now > this.dmgUntil) this.dmgdir.style.opacity = '0';
  }

  setSpread(px: number): void {
    const [top, bottom, left, right] = this.crosshairArms;
    top.style.top = `${-7 - px}px`;
    bottom.style.top = `${7 + px}px`;
    left.style.left = `${-7 - px}px`;
    right.style.left = `${7 + px}px`;
  }

  setScope(show: boolean): void {
    this.scope.style.opacity = show ? '1' : '0';
  }

  setScoreboard(show: boolean, players: PlayerSnap[], selfId: number, hostId: number | null = null): void {
    this.scoreboard.style.display = show ? 'flex' : 'none';
    if (!show) return;
    const sorted = [...players].sort((a, b) => b.k - a.k || a.d - b.d);
    this.scoreboardBody.innerHTML = '';
    for (const p of sorted) {
      const tr = document.createElement('tr');
      if (p.id === selfId) tr.className = 'me';
      const acc = p.sf > 0 ? Math.round((p.sh / p.sf) * 100) : 0;
      const hostTag = hostId !== null && p.id === hostId ? ' <span class="hosttag">房主</span>' : '';
      tr.innerHTML = `<td>${escapeHtml(p.name)}${hostTag}</td><td>${p.k}</td><td>${p.d}</td><td>${p.bs}</td><td>${acc}%</td>`;
      this.scoreboardBody.appendChild(tr);
    }
  }

  hitmarkerShow(head: boolean): void {
    this.hitAt = performance.now();
    this.hitmarker.classList.toggle('head', head);
  }

  damageFlash(): void {
    this.vignette.style.opacity = '0.9';
    setTimeout(() => (this.vignette.style.opacity = '0'), 180);
  }

  flash(ms: number): void {
    this.flashStart = performance.now();
    this.flashUntil = this.flashStart + ms;
  }

  setCook(text: string | null): void {
    if (text) {
      this.equip.textContent = `烹煮 ${text}s 后松手投出！`;
      this.equip.style.color = '#ff8f6a';
    } else {
      this.equip.style.color = '';
    }
  }

  setBreath(v: number | null): void {
    this.breathbar.style.opacity = v !== null ? '1' : '0';
    if (v !== null) {
      this.breathfill.style.width = `${Math.round(v * 100)}%`;
      this.breathfill.style.background = v > 0.35 ? '#5ac8dc' : '#d84f3f';
    }
  }

  damageDir(angle: number): void {
    this.dmgUntil = performance.now() + 1000;
    this.dmgdir.style.opacity = '1';
    this.dmgdir.style.transform = `translate(-50%, -50%) rotate(${(angle * 180) / Math.PI}deg)`;
  }

  addKill(killer: string, victim: string, hs = false, cause: string = 'ar'): void {
    const li = document.createElement('li');
    if (cause === 'suicide') {
      li.innerHTML = `<span class="kname">${escapeHtml(victim)}</span> <span class="cause">自爆</span>`;
    } else {
      const hsTag = hs ? '<span class="hs">爆头</span>' : '';
      li.innerHTML = `<span class="kname">${escapeHtml(killer)}</span> <span class="cause">${causeLabel(cause)}</span>${hsTag} <span class="kname">${escapeHtml(victim)}</span>`;
    }
    this.killfeed.prepend(li);
    while (this.killfeed.children.length > 5) this.killfeed.removeChild(this.killfeed.lastChild!);
    setTimeout(() => li.remove(), 6000);
  }

  showDeath(killerName: string, cause: string = 'ar'): void {
    this.deathAt = performance.now();
    const how = deathText(cause);
    this.deathtext.textContent = how ? `你被 ${killerName} ${how}` : `你被 ${killerName} 击杀`;
    this.deathoverlay.classList.add('show');
  }

  hideDeath(): void {
    this.deathAt = 0;
    this.deathoverlay.classList.remove('show');
  }

  showKill(victimName: string): void {
    this.killmsg.textContent = `☠ 击杀 ${victimName}`;
    this.killmsg.style.opacity = '1';
    setTimeout(() => (this.killmsg.style.opacity = '0'), 1500);
  }

  showEnd(win: boolean, standings: Standing[], selfId: number): void {
    this.endtitle.textContent = win ? '🏆 胜利' : '比赛结束';
    this.endbody.innerHTML = '';
    standings.forEach((s, i) => {
      const tr = document.createElement('tr');
      if (s.id === selfId) tr.className = 'me';
      const acc = s.sf > 0 ? Math.round((s.sh / s.sf) * 100) : 0;
      tr.innerHTML = `<td>${i + 1}</td><td>${escapeHtml(s.name)}</td><td>${s.k}</td><td>${s.d}</td><td>${s.bs}</td><td>${acc}%</td>`;
      this.endbody.appendChild(tr);
    });
    this.endoverlay.classList.add('show');
  }

  hideEnd(): void {
    this.endoverlay.classList.remove('show');
  }

  reset(): void {
    this.killfeed.innerHTML = '';
    this.hideDeath();
    this.hideEnd();
    this.deathAt = 0;
  }
}

function el<T extends HTMLElement>(id: string): T {
  return document.getElementById(id) as T;
}

function weaponName(w: string): string {
  return (
    {
      ar: '突击步枪',
      smg: '冲锋枪',
      lmg: '轻机枪',
      dmr: '射手步枪',
      sg: '霰弹枪',
      sr: '狙击枪',
      pistol: '手枪',
    } as Record<string, string>
  )[w] ?? w;
}

function causeLabel(cause: string): string {
  switch (cause) {
    case 'melee':
      return '近战';
    case 'grenade':
      return '手雷';
    case 'airstrike':
      return '空袭';
    case 'cluster':
      return '集束';
    case 'suicide':
      return '自爆';
    case 'sg':
      return '霰弹枪';
    case 'sr':
      return '狙击枪';
    case 'ar':
      return '步枪';
    case 'smg':
      return '冲锋枪';
    case 'lmg':
      return '轻机枪';
    case 'dmr':
      return '射手步枪';
    case 'pistol':
      return '手枪';
    default:
      return '击杀';
  }
}

function deathText(cause: string): string {
  switch (cause) {
    case 'melee':
      return '近战击杀';
    case 'grenade':
      return '的手雷炸死';
    case 'airstrike':
      return '的空袭炸死';
    case 'cluster':
      return '的集束炸弹炸死';
    case 'barrel':
      return '引爆的油桶炸死';
    case 'suicide':
      return '自爆身亡';
    default:
      return '击杀';
  }
}

function fmtTime(sec: number): string {
  const s = Math.max(0, Math.ceil(sec));
  return `${Math.floor(s / 60)}:${String(s % 60).padStart(2, '0')}`;
}

function escapeHtml(s: string): string {
  return s.replace(/[&<>"']/g, (c) => `&#${c.charCodeAt(0)};`);
}
