// Persistência local (navegador). Perfis, fonte e favoritos. Nunca salva usuário nem senha.

export type SourceKind = "demo" | "xtream" | "m3u";

export interface SourceConfig {
  kind: SourceKind;
  name: string;
  url: string; // servidor (Xtream) ou URL M3U
  epgUrl: string; // opcional, acompanha a fonte
}

export interface Profile {
  id: string;
  name: string;
  color: string;
  source: SourceConfig;
  favTitles: string[];
  favChannels: string[];
}

export interface StoredProfiles {
  profiles: Profile[];
  activeId: string | null;
}

export interface Settings {
  tvMode: boolean;
  reduceMotion: boolean;
  sounds: boolean;
}

export const PROFILE_COLORS = ["#7942ec", "#c1f873", "#f577d2", "#5ec8ff", "#ffb36b", "#ff6b8a"];
export const MAX_PROFILES = 5;
export const DEMO_SOURCE: SourceConfig = { kind: "demo", name: "Demonstração", url: "", epgUrl: "" };

const PROFILES_KEY = "zimtv.profiles.v1";
const SETTINGS_KEY = "zimtv.settings.v1";

export function newId(): string {
  return Math.random().toString(36).slice(2, 10) + Date.now().toString(36);
}

export function makeProfile(name: string, color: string): Profile {
  return { id: newId(), name, color, source: { ...DEMO_SOURCE }, favTitles: [], favChannels: [] };
}

export function loadProfiles(): StoredProfiles {
  try {
    const raw = window.localStorage.getItem(PROFILES_KEY);
    if (raw) {
      const parsed = JSON.parse(raw) as StoredProfiles;
      if (Array.isArray(parsed.profiles) && parsed.profiles.length > 0) {
        return parsed;
      }
    }
  } catch {
    // dados corrompidos: começa do zero
  }
  const principal = makeProfile("Principal", PROFILE_COLORS[0]);
  return { profiles: [principal], activeId: null };
}

export function saveProfiles(data: StoredProfiles): void {
  try {
    window.localStorage.setItem(PROFILES_KEY, JSON.stringify(data));
  } catch {
    // armazenamento indisponível: a prévia continua funcionando em memória
  }
}

export function loadSettings(): Settings {
  const defaults: Settings = { tvMode: true, reduceMotion: false, sounds: false };
  try {
    const raw = window.localStorage.getItem(SETTINGS_KEY);
    return raw ? { ...defaults, ...(JSON.parse(raw) as Partial<Settings>) } : defaults;
  } catch {
    return defaults;
  }
}

export function saveSettings(s: Settings): void {
  try {
    window.localStorage.setItem(SETTINGS_KEY, JSON.stringify(s));
  } catch {
    // ignora
  }
}
