import { useEffect, useState, type CSSProperties } from "react";
import type { SourceConfig } from "./zim-store";

// Utilitários e peças pequenas compartilhadas pelas telas do ZIMTV.

export type Section = "home" | "live" | "movies" | "series" | "list" | "search" | "settings";

export const SECTIONS: Section[] = ["home", "live", "movies", "series", "list", "search", "settings"];

export const SECTION_LABEL: Record<Section, string> = {
  home: "Início",
  live: "Ao vivo",
  movies: "Filmes",
  series: "Séries",
  list: "Minha lista",
  search: "Busca",
  settings: "Configurações",
};

export const NOTE_NO_SIGNAL = "Demonstração: nenhum vídeo ou serviço IPTV está conectado.";

export function gradientStyle(colors: [string, string]): CSSProperties {
  return { background: `linear-gradient(135deg, ${colors[0]} 0%, ${colors[1]} 100%)` };
}

export function initialOf(name: string): string {
  const t = name.trim();
  return t ? t.charAt(0).toUpperCase() : "?";
}

export function parseHttpUrl(value: string): URL | null {
  try {
    const u = new URL(value.trim());
    return u.protocol === "http:" || u.protocol === "https:" ? u : null;
  } catch {
    return null;
  }
}

export function isHttpUrl(value: string): boolean {
  return parseHttpUrl(value) !== null;
}

const CREDENTIAL_PARAMS = ["username", "password", "user", "pass"];

// Bloqueia URLs com usuário/senha embutidos: a prévia não guarda credenciais.
export function containsCredentials(value: string): boolean {
  const u = parseHttpUrl(value);
  if (!u) return false;
  if (u.username || u.password) return true;
  return CREDENTIAL_PARAMS.some((k) => u.searchParams.has(k));
}

export function sourceLabel(source: SourceConfig): string {
  if (source.kind === "demo") return "Demonstração";
  const kind = source.kind === "xtream" ? "Xtream" : "M3U";
  return source.name ? `${kind} · ${source.name}` : kind;
}

export function formatTime(d: Date): string {
  return d.toLocaleTimeString("pt-BR", { hour: "2-digit", minute: "2-digit" });
}

export function useClock(): Date {
  const [now, setNow] = useState(() => new Date());
  useEffect(() => {
    const id = window.setInterval(() => setNow(new Date()), 15000);
    return () => window.clearInterval(id);
  }, []);
  return now;
}

export function Logo({ height = 44 }: { height?: number }) {
  return <img className="zim-logo-img" src="/images/zimtv-logo.png" alt="ZIMTV" style={{ height }} />;
}

export function Avatar({ name, color, size = 34 }: { name: string; color: string; size?: number }) {
  return (
    <span
      className="dash-avatar"
      aria-hidden="true"
      style={{ background: color, width: size, height: size, fontSize: size * 0.42 }}
    >
      {initialOf(name)}
    </span>
  );
}
