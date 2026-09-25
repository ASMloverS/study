import { defaultSettings, type Settings } from './input';

const KEY = 'codcopy.settings.v1';

export function loadSettings(): Partial<Settings> {
  try {
    const raw = localStorage.getItem(KEY);
    if (!raw) return {};
    const parsed = JSON.parse(raw);
    if (typeof parsed !== 'object' || parsed === null) return {};
    const out: Partial<Settings> = {};
    for (const k of Object.keys(defaultSettings) as (keyof Settings)[]) {
      if (typeof parsed[k] === typeof defaultSettings[k]) out[k] = parsed[k];
    }
    return out;
  } catch {
    return {};
  }
}

export function saveSettings(settings: Settings): void {
  try {
    localStorage.setItem(KEY, JSON.stringify(settings));
  } catch {
    void 0;
  }
}
