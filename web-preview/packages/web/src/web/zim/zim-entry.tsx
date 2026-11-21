import { useEffect, useRef, useState, type FormEvent } from "react";
import {
  MAX_PROFILES,
  PROFILE_COLORS,
  type Profile,
  type SourceConfig,
  type SourceKind,
} from "./zim-store";
import { Avatar, Logo, containsCredentials, initialOf, isHttpUrl, sourceLabel } from "./zim-ui";

// Telas de entrada: perfis, fonte, formulários, carregamento e edição de perfil.

// Erros que podem aparecer no carregamento. Só valem para fontes reais, que a prévia não conecta.
export function loadError(source: SourceConfig): string | null {
  if (source.kind === "demo") return null;
  if (!isHttpUrl(source.url)) return "O endereço da lista não é uma URL http(s) válida.";
  if (source.epgUrl && !isHttpUrl(source.epgUrl)) return "O endereço do EPG não é uma URL http(s) válida.";
  if (containsCredentials(source.url)) {
    return "A URL contém usuário ou senha. Remova-os: esta prévia não guarda credenciais.";
  }
  return null;
}

interface ProfilesProps {
  profiles: Profile[];
  manage: boolean;
  onSelect: (id: string) => void;
  onEdit: (id: string) => void;
  onAdd: () => void;
  onToggleManage: () => void;
}

export function ProfilesScreen({ profiles, manage, onSelect, onEdit, onAdd, onToggleManage }: ProfilesProps) {
  const canAdd = profiles.length < MAX_PROFILES;
  return (
    <section className="entry">
      <div className="entry-mural" style={{ backgroundImage: "url(/images/profiles-mural.jpg)" }} aria-hidden="true" />
      <div className="entry-shade" aria-hidden="true" />
      <Logo height={48} />
      <h1 className="entry-title">Quem está assistindo?</h1>
      <p className="muted">Perfis ficam só neste navegador. Máximo de {MAX_PROFILES}.</p>
      <div className="profile-grid">
        {profiles.map((p, i) => (
          <button
            key={p.id}
            type="button"
            className="zf profile-card"
            data-autofocus={i === 0 ? "true" : undefined}
            aria-label={`${manage ? "Editar" : "Entrar como"} ${p.name}`}
            onClick={() => (manage ? onEdit(p.id) : onSelect(p.id))}
          >
            <span className="profile-avatar" style={{ background: p.color }}>
              {initialOf(p.name)}
            </span>
            <span className="profile-name">{p.name}</span>
            <span className="profile-sub">{sourceLabel(p.source)}</span>
            {manage && <span className="profile-edit">Editar</span>}
          </button>
        ))}
        {canAdd && (
          <button type="button" className="zf profile-card profile-add" onClick={onAdd}>
            <span className="profile-avatar profile-plus">+</span>
            <span className="profile-name">Adicionar perfil</span>
          </button>
        )}
      </div>
      <div className="entry-actions">
        <button type="button" className="zf btn btn-ghost" onClick={onToggleManage}>
          {manage ? "Concluir" : "Gerenciar perfis"}
        </button>
      </div>
    </section>
  );
}

interface SourceScreenProps {
  profile: Profile;
  onChoose: (kind: SourceKind) => void;
  onBack: () => void;
}

const SOURCE_OPTIONS: { kind: SourceKind; title: string; text: string }[] = [
  {
    kind: "demo",
    title: "Demonstração",
    text: "Explore a interface com catálogo e canais de exemplo. Nenhum serviço é conectado.",
  },
  {
    kind: "xtream",
    title: "Lista Xtream",
    text: "Nome da lista e endereço do servidor. Usuário e senha não são guardados.",
  },
  {
    kind: "m3u",
    title: "Lista M3U",
    text: "Nome da lista e URL M3U. Nada é baixado nesta prévia.",
  },
];

export function SourceScreen({ profile, onChoose, onBack }: SourceScreenProps) {
  return (
    <section className="entry">
      <Logo height={44} />
      <h1 className="entry-title">Sua fonte</h1>
      <p className="muted">
        Perfil <strong>{profile.name}</strong> · atual: {sourceLabel(profile.source)}
      </p>
      <div className="source-grid">
        {SOURCE_OPTIONS.map((o, i) => (
          <button
            key={o.kind}
            type="button"
            className="zf source-card"
            data-autofocus={i === 0 ? "true" : undefined}
            onClick={() => onChoose(o.kind)}
          >
            <strong>{o.title}</strong>
            <span>{o.text}</span>
          </button>
        ))}
      </div>
      <div className="entry-actions">
        <button type="button" className="zf btn btn-ghost" onClick={onBack}>
          Voltar
        </button>
      </div>
    </section>
  );
}

interface SourceFormProps {
  kind: "xtream" | "m3u";
  onSubmit: (source: SourceConfig) => void;
  onBack: () => void;
}

// Usuário e senha ficam só no estado do formulário e somem ao sair da tela.
export function SourceForm({ kind, onSubmit, onBack }: SourceFormProps) {
  const isXtream = kind === "xtream";
  const [name, setName] = useState("Minha lista");
  const [url, setUrl] = useState("");
  const [epg, setEpg] = useState("");
  const [user, setUser] = useState("");
  const [pass, setPass] = useState("");
  const [error, setError] = useState<string | null>(null);

  const submit = (e: FormEvent) => {
    e.preventDefault();
    const n = name.trim();
    const u = url.trim();
    const g = epg.trim();
    if (!n) {
      setError("Dê um nome para a lista.");
      return;
    }
    if (!isHttpUrl(u)) {
      setError(
        isXtream
          ? "Informe o endereço do servidor começando com http:// ou https://."
          : "Informe a URL M3U começando com http:// ou https://.",
      );
      return;
    }
    if (g && !isHttpUrl(g)) {
      setError("A URL do EPG precisa começar com http:// ou https://.");
      return;
    }
    if (containsCredentials(u)) {
      setError("Remova usuário e senha da URL. Esta prévia não guarda credenciais.");
      return;
    }
    setUser("");
    setPass("");
    onSubmit({ kind, name: n.slice(0, 40), url: u, epgUrl: g });
  };

  return (
    <section className="entry">
      <Logo height={44} />
      <h1 className="entry-title">{isXtream ? "Lista Xtream" : "Lista M3U"}</h1>
      <form className="entry-form" onSubmit={submit} autoComplete="off">
        <label className="form-field">
          <span>Nome da lista</span>
          <input className="zf field" aria-label="Nome da lista" data-autofocus="true" maxLength={40} value={name} onChange={(e) => setName(e.target.value)} />
        </label>
        {isXtream ? (
          <label className="form-field">
            <span>Endereço do servidor</span>
            <input className="zf field" aria-label="Endereço do servidor" placeholder="http://servidor:porta" value={url} onChange={(e) => setUrl(e.target.value)} />
          </label>
        ) : (
          <label className="form-field">
            <span>URL M3U</span>
            <input className="zf field" aria-label="URL M3U" placeholder="https://exemplo.com/lista.m3u" value={url} onChange={(e) => setUrl(e.target.value)} />
          </label>
        )}
        {isXtream && (
          <>
            <label className="form-field">
              <span>Usuário (exemplo)</span>
              <input className="zf field" aria-label="Usuário (exemplo)" name="zim-example-user" value={user} onChange={(e) => setUser(e.target.value)} />
            </label>
            <label className="form-field">
              <span>Senha (exemplo)</span>
              <input className="zf field" aria-label="Senha (exemplo)" type="password" name="zim-example-pass" value={pass} onChange={(e) => setPass(e.target.value)} />
            </label>
            <p className="form-note">Usuário e senha são só exemplo: não são enviados nem guardados.</p>
          </>
        )}
        <label className="form-field">
          <span>URL do EPG (opcional)</span>
          <input className="zf field" aria-label="URL do EPG (opcional)" placeholder="https://exemplo.com/epg.xml" value={epg} onChange={(e) => setEpg(e.target.value)} />
        </label>
        {error && (
          <p className="form-error" role="alert">
            {error}
          </p>
        )}
        <div className="editor-actions">
          <button type="button" className="zf btn btn-ghost" onClick={onBack}>
            Voltar
          </button>
          <button type="submit" className="zf btn btn-primary">
            Continuar
          </button>
        </div>
      </form>
      <p className="disclaimer">Prévia visual: nenhuma conexão é feita com o servidor.</p>
    </section>
  );
}

const STAGES = ["Perfil", "Sua fonte", "Catálogo"];
const STAGE_MESSAGES = ["Organizando a prévia.", "Preparando filmes e séries.", "Finalizando a interface."];

interface LoadingProps {
  profile: Profile;
  source: SourceConfig;
  onDone: () => void;
  onCancel: () => void;
  onRetry: () => void;
}

// Progresso de 0 a 100 em cerca de 4 segundos (40 passos de 100 ms).
export function LoadingScreen({ profile, source, onDone, onCancel, onRetry }: LoadingProps) {
  const error = loadError(source);
  const [progress, setProgress] = useState(0);
  const doneRef = useRef(onDone);
  doneRef.current = onDone;

  useEffect(() => {
    if (error) return;
    if (progress >= 100) {
      const t = window.setTimeout(() => doneRef.current(), 300);
      return () => window.clearTimeout(t);
    }
    const t = window.setTimeout(() => setProgress((p) => Math.min(100, p + 2.5)), 100);
    return () => window.clearTimeout(t);
  }, [progress, error]);

  if (error) {
    return (
      <section className="entry">
        <Logo height={44} />
        <h1 className="entry-title">Não foi possível preparar a prévia</h1>
        <p className="form-error" role="alert">
          {error}
        </p>
        <div className="entry-actions">
          <button type="button" className="zf btn btn-primary" data-autofocus="true" onClick={onRetry}>
            Tentar de novo
          </button>
          <button type="button" className="zf btn btn-ghost" onClick={onCancel}>
            Voltar
          </button>
        </div>
      </section>
    );
  }

  const stage = progress < 34 ? 0 : progress < 67 ? 1 : 2;
  const pct = Math.round(progress);

  return (
    <section className="entry entry-loading">
      <div className="tv-ring" aria-hidden="true">
        <span />
      </div>
      <Logo height={56} />
      <h1 className="entry-title">Preparando sua experiência</h1>
      <p className="muted">
        {profile.name} · {sourceLabel(source)}
      </p>
      <div className="load-bar">
        <span style={{ width: `${progress}%` }} />
      </div>
      <p className="load-pct">{pct}%</p>
      <ol className="load-stages">
        {STAGES.map((s, i) => (
          <li key={s} className={i < stage ? "done" : i === stage ? "current" : undefined}>
            {s}
          </li>
        ))}
      </ol>
      <p className="load-msg" aria-live="polite">
        {STAGE_MESSAGES[stage]}
      </p>
      <p className="disclaimer">Demonstração: nenhuma lista conectada.</p>
      <div className="entry-actions">
        <button type="button" className="zf btn btn-ghost" data-autofocus="true" onClick={onCancel}>
          Cancelar
        </button>
      </div>
    </section>
  );
}

interface EditorProps {
  profile: Profile | null; // null = novo perfil
  canDelete: boolean;
  onSave: (name: string, color: string) => void;
  onDelete: () => void;
  onClose: () => void;
}

export function ProfileEditor({ profile, canDelete, onSave, onDelete, onClose }: EditorProps) {
  const [name, setName] = useState(profile?.name ?? "");
  const [color, setColor] = useState(profile?.color ?? PROFILE_COLORS[0]);
  const [error, setError] = useState<string | null>(null);

  const submit = (e: FormEvent) => {
    e.preventDefault();
    const n = name.trim();
    if (!n) {
      setError("Dê um nome ao perfil.");
      return;
    }
    onSave(n.slice(0, 24), color);
  };

  return (
    <div className="modal-backdrop" data-modal-open="true">
      <form className="editor" aria-label={profile ? "Editar perfil" : "Novo perfil"} onSubmit={submit}>
        <h2>{profile ? "Editar perfil" : "Novo perfil"}</h2>
        <label className="form-field">
          <span>Nome</span>
          <input className="zf field" aria-label="Nome" data-autofocus="true" maxLength={24} value={name} onChange={(e) => setName(e.target.value)} />
        </label>
        <fieldset className="swatches">
          <legend>Cor</legend>
          {PROFILE_COLORS.map((c) => (
            <button
              key={c}
              type="button"
              className="zf swatch"
              style={{ background: c }}
              aria-pressed={color === c}
              aria-label={`Cor ${c}`}
              onClick={() => setColor(c)}
            />
          ))}
        </fieldset>
        {profile && <Avatar name={name || profile.name} color={color} size={48} />}
        {error && (
          <p className="form-error" role="alert">
            {error}
          </p>
        )}
        <div className="editor-actions">
          {profile && canDelete && (
            <button type="button" className="zf btn btn-danger" onClick={onDelete}>
              Excluir perfil
            </button>
          )}
          <button type="button" className="zf btn btn-ghost" onClick={onClose}>
            Cancelar
          </button>
          <button type="submit" className="zf btn btn-primary">
            Salvar
          </button>
        </div>
      </form>
    </div>
  );
}
