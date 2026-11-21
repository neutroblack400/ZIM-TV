import { createContext, useContext, useEffect, useRef, useState, type ReactNode } from "react";

// Navegação por foco (teclado, controle) + modo TV + rodapé de controles.
// Elementos navegáveis recebem a classe .zf. Modais marcam [data-modal-open]
// e o foco fica restrito ao modal aberto mais alto.

export type Dir = "up" | "down" | "left" | "right";
export type Action =
  | Dir
  | "select"
  | "back"
  | "search"
  | "settings"
  | "prevSection"
  | "nextSection";

interface GamepadInfo {
  supported: boolean;
  name: string | null;
}

interface TvContextValue {
  tvMode: boolean;
  gamepad: GamepadInfo;
}

const TvContext = createContext<TvContextValue>({ tvMode: true, gamepad: { supported: false, name: null } });
export const useTv = () => useContext(TvContext);

interface TvShellProps {
  children: ReactNode;
  tvMode: boolean;
  reduceMotion: boolean;
  onToggleTv: () => void;
  onAction: (action: Action) => void; // ações de alto nível: busca, config, seção, voltar
  focusKey: string; // muda quando a tela/modal muda: o foco inicial é reposicionado
}

function scopeRoot(): ParentNode {
  const modals = document.querySelectorAll<HTMLElement>("[data-modal-open]");
  return modals.length ? modals[modals.length - 1] : document;
}

function visibleFocusables(scope: ParentNode): HTMLElement[] {
  return Array.from(scope.querySelectorAll<HTMLElement>(".zf")).filter(
    (el) => !el.hasAttribute("disabled") && el.getClientRects().length > 0,
  );
}

function moveFocus(dir: Dir): void {
  const els = visibleFocusables(scopeRoot());
  if (els.length === 0) return;
  const cur = document.activeElement as HTMLElement | null;
  if (!cur || !els.includes(cur)) {
    els[0].focus();
    return;
  }
  const r = cur.getBoundingClientRect();
  const cx = r.left + r.width / 2;
  const cy = r.top + r.height / 2;
  let best: HTMLElement | null = null;
  let bestScore = Infinity;
  for (const el of els) {
    if (el === cur) continue;
    const b = el.getBoundingClientRect();
    const dx = b.left + b.width / 2 - cx;
    const dy = b.top + b.height / 2 - cy;
    let primary: number;
    let secondary: number;
    if (dir === "right") {
      if (dx <= 4) continue;
      primary = dx;
      secondary = Math.abs(dy);
    } else if (dir === "left") {
      if (dx >= -4) continue;
      primary = -dx;
      secondary = Math.abs(dy);
    } else if (dir === "down") {
      if (dy <= 4) continue;
      primary = dy;
      secondary = Math.abs(dx);
    } else {
      if (dy >= -4) continue;
      primary = -dy;
      secondary = Math.abs(dx);
    }
    const score = primary + secondary * 2;
    if (score < bestScore) {
      bestScore = score;
      best = el;
    }
  }
  best?.focus();
}

function clickFocused(): void {
  const cur = document.activeElement as HTMLElement | null;
  if (cur && cur.classList.contains("zf")) cur.click();
}

const KEY_ACTIONS: Record<string, Action> = {
  ArrowUp: "up",
  ArrowDown: "down",
  ArrowLeft: "left",
  ArrowRight: "right",
  Enter: "select",
  Escape: "back",
  F2: "search",
  F3: "settings",
  PageUp: "prevSection",
  PageDown: "nextSection",
};

// Botões do padrão "standard" do Gamepad API. Mapeamento sujeito a validação no console.
const PAD_BUTTONS: Record<number, Action> = {
  12: "up",
  13: "down",
  14: "left",
  15: "right",
  0: "select",
  1: "back",
  2: "settings",
  3: "search",
  4: "prevSection",
  5: "nextSection",
};

export function TvShell({ children, tvMode, reduceMotion, onToggleTv, onAction, focusKey }: TvShellProps) {
  const actionRef = useRef(onAction);
  actionRef.current = onAction;
  const [gamepad, setGamepad] = useState<GamepadInfo>({ supported: true, name: null });
  const [fsMessage, setFsMessage] = useState<string | null>(null);

  // Teclado
  useEffect(() => {
    const onKey = (e: KeyboardEvent) => {
      const typing = e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement;
      if (e.key === "Backspace" && !typing) {
        e.preventDefault();
        actionRef.current("back");
        return;
      }
      const action = KEY_ACTIONS[e.key];
      if (!action) return;
      if (typing && (action === "left" || action === "right" || action === "select")) return;
      // Enter em botão já dispara o clique nativo; não duplicar.
      if (action === "select") return;
      if (action === "back") {
        actionRef.current("back");
        return;
      }
      e.preventDefault();
      dispatchAction(action, actionRef.current);
    };
    window.addEventListener("keydown", onKey);
    return () => window.removeEventListener("keydown", onKey);
  }, []);

  // Gamepad (polling, pois a API não emite eventos de botão)
  useEffect(() => {
    if (typeof navigator === "undefined" || typeof navigator.getGamepads !== "function") {
      setGamepad({ supported: false, name: null });
      return;
    }
    let raf = 0;
    const prevButtons: Record<number, boolean> = {};
    let nextRepeat = 0;
    let lastName: string | null = null;
    const loop = (now: number) => {
      let pad: Gamepad | null = null;
      try {
        pad = Array.from(navigator.getGamepads()).find((p) => p && p.connected) ?? null;
      } catch {
        pad = null;
      }
      const name = pad ? pad.id : null;
      if (name !== lastName) {
        lastName = name;
        setGamepad({ supported: true, name });
      }
      if (pad) {
        pad.buttons.forEach((b, i) => {
          const pressed = b.pressed;
          if (pressed && !prevButtons[i]) {
            const action = PAD_BUTTONS[i];
            if (action) dispatchAction(action, actionRef.current);
          }
          prevButtons[i] = pressed;
        });
        const ax = pad.axes[0] ?? 0;
        const ay = pad.axes[1] ?? 0;
        if (now > nextRepeat) {
          let stick: Dir | null = null;
          if (Math.abs(ax) > 0.6 || Math.abs(ay) > 0.6) {
            stick = Math.abs(ax) > Math.abs(ay) ? (ax > 0 ? "right" : "left") : ay > 0 ? "down" : "up";
          }
          if (stick) {
            dispatchAction(stick, actionRef.current);
            nextRepeat = now + 220;
          }
        }
      }
      raf = requestAnimationFrame(loop);
    };
    raf = requestAnimationFrame(loop);
    return () => cancelAnimationFrame(raf);
  }, []);

  // Foco inicial ao trocar de tela ou abrir/fechar modal
  useEffect(() => {
    const t = window.setTimeout(() => {
      const scope = scopeRoot();
      const auto = scope.querySelector<HTMLElement>("[data-autofocus]");
      if (auto && auto.getClientRects().length > 0) {
        auto.focus();
        return;
      }
      const first = visibleFocusables(scope)[0];
      first?.focus();
    }, 30);
    return () => window.clearTimeout(t);
  }, [focusKey]);

  const toggleFullscreen = async () => {
    try {
      if (document.fullscreenElement) {
        await document.exitFullscreen();
      } else {
        await document.documentElement.requestFullscreen();
      }
      setFsMessage(null);
    } catch {
      setFsMessage("O navegador bloqueou a tela cheia. Use F11 ou a opção do navegador.");
    }
  };

  const padLabel = !gamepad.supported
    ? "Gamepad API indisponível neste navegador"
    : gamepad.name
      ? `Controle detectado: ${gamepad.name}`
      : "Controle não detectado. Pressione um botão no controle.";

  return (
    <TvContext.Provider value={{ tvMode, gamepad }}>
      <div className={`zim-root ${tvMode ? "tv-mode" : "web-mode"} ${reduceMotion ? "reduce-motion" : ""}`}>
        <div className="zim-stage">{children}</div>
        <footer className="tv-footer" aria-label="Controles">
          <div className="tv-symbols">
            <span><i className="sym sym-dpad">✥</i> Mover</span>
            <span><i className="sym sym-x">✕</i> Selecionar</span>
            <span><i className="sym sym-o">○</i> Voltar</span>
            <span><i className="sym sym-tri">△</i> Pesquisar</span>
            <span><i className="sym sym-sq">□</i> Configurações</span>
            <span><i className="sym">L1 R1</i> Seções</span>
          </div>
          <div className="tv-controls">
            <span className="tv-pad-status">{padLabel}</span>
            <button type="button" className="zf tv-btn" onClick={onToggleTv}>
              {tvMode ? "Modo TV" : "Modo web"}
            </button>
            <button type="button" className="zf tv-btn" onClick={toggleFullscreen}>
              Tela cheia
            </button>
          </div>
          {fsMessage && <output className="tv-fs-msg">{fsMessage}</output>}
        </footer>
      </div>
    </TvContext.Provider>
  );
}

function dispatchAction(action: Action, onAction: (a: Action) => void): void {
  switch (action) {
    case "up":
    case "down":
    case "left":
    case "right":
      moveFocus(action);
      return;
    case "select":
      clickFocused();
      return;
    default:
      onAction(action);
  }
}
