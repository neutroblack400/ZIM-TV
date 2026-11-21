import { useMemo, useState } from "react";
import { GENRES, TITLES, CHANNELS, type ZChannel, type ZTitle, type TitleKind } from "./zim-data";
import type { Profile, Settings } from "./zim-store";
import { Avatar, NOTE_NO_SIGNAL, SECTION_LABEL, SECTIONS, type Section, Logo, formatTime, gradientStyle, sourceLabel, useClock } from "./zim-ui";
import { useTv } from "./tv-shell";

// Painel principal: cabeçalho, início, catálogos, lista, busca, configurações, detalhes e player.

interface DashHeaderProps {
  section: Section;
  onSection: (s: Section) => void;
  profile: Profile;
  onSwitchProfile: () => void;
  onOpenSource: () => void;
}

export function DashHeader({ section, onSection, profile, onSwitchProfile, onOpenSource }: DashHeaderProps) {
  const now = useClock();
  return (
    <header className="dash-header">
      <Logo height={36} />
      <nav className="dash-nav" aria-label="Seções">
        {SECTIONS.map((s) => (
          <button
            key={s}
            type="button"
            className="zf dash-nav-item"
            aria-current={s === section ? "page" : undefined}
            onClick={() => onSection(s)}
          >
            {SECTION_LABEL[s]}
          </button>
        ))}
      </nav>
      <div className="dash-actions">
        <span className="clock" aria-label="Horário">{formatTime(now)}</span>
        <button type="button" className="zf dash-profile" onClick={onSwitchProfile} aria-label={`Perfil ${profile.name}. Trocar perfil`}>
          <Avatar name={profile.name} color={profile.color} size={30} />
          <span>{profile.name}</span>
        </button>
        <button type="button" className="zf btn btn-ghost" onClick={onOpenSource}>
          Fonte: {sourceLabel(profile.source)}
        </button>
        <button type="button" className="zf btn btn-ghost" onClick={onSwitchProfile}>
          Trocar perfil
        </button>
      </div>
    </header>
  );
}

interface TitleCardProps {
  title: ZTitle;
  onOpen: (id: string) => void;
  autofocus?: boolean;
}

export function TitleCard({ title, onOpen, autofocus }: TitleCardProps) {
  return (
    <button
      type="button"
      className="zf tcard"
      data-autofocus={autofocus ? "true" : undefined}
      aria-label={`${title.name}, ${title.kind === "series" ? "série" : "filme"}`}
      onClick={() => onOpen(title.id)}
    >
      <span className="tcard-art" style={gradientStyle(title.colors)} aria-hidden="true">
        <span className="tcard-shade" />
        <span className="tcard-label">{title.name}</span>
      </span>
      <span className="tcard-title">{title.name}</span>
      <span className="tcard-genre">{title.genre}</span>
      {title.progress > 0 && (
        <span className="tcard-progress" aria-hidden="true">
          <span style={{ width: `${Math.round(title.progress * 100)}%` }} />
        </span>
      )}
    </button>
  );
}

interface HomeProps {
  onOpenDetails: (id: string) => void;
  onPlay: (id: string) => void;
  onGoLive: () => void;
}

export function HomeView({ onOpenDetails, onPlay, onGoLive }: HomeProps) {
  const [idx, setIdx] = useState(0);
  const hero = TITLES[idx];
  const shift = (d: number) => setIdx((i) => (i + d + TITLES.length) % TITLES.length);
  const continuing = TITLES.filter((t) => t.progress > 0);

  return (
    <div className="home">
      <section className="hero" aria-label={`Destaque: ${hero.name}`}>
        <div className="hero-bg" style={{ backgroundImage: "url(/images/hero-bg.jpg)" }} aria-hidden="true" />
        <div className="hero-shade" style={{ background: `linear-gradient(90deg, ${hero.colors[0]}88, transparent 70%)` }} aria-hidden="true" />
        <div className="hero-body">
          <span className={`badge ${idx % 2 ? "badge-pink" : "badge-lime"}`}>{hero.match}% de destaque · ilustrativo</span>
          <h2 className="hero-title">{hero.name}</h2>
          <p className="hero-meta">
            {hero.year} · {hero.age}+ · {hero.duration} · {hero.genre}
          </p>
          <p className="hero-synopsis">{hero.synopsis}</p>
          <div className="hero-actions">
            <button type="button" className="zf btn btn-primary" data-autofocus="true" onClick={() => onPlay(hero.id)}>
              ▶ Assistir prévia
            </button>
            <button type="button" className="zf btn btn-ghost" onClick={() => onOpenDetails(hero.id)}>
              Mais informações
            </button>
          </div>
        </div>
        <div className="hero-controls">
          <button type="button" className="zf btn btn-ghost" aria-label="Destaque anterior" onClick={() => shift(-1)}>
            ‹
          </button>
          <button type="button" className="zf btn btn-ghost" aria-label="Próximo destaque" onClick={() => shift(1)}>
            ›
          </button>
          <div className="hero-dots">
            {TITLES.map((t, i) => (
              <button
                key={t.id}
                type="button"
                className="zf dot"
                aria-current={i === idx ? "true" : undefined}
                aria-label={`Destaque ${i + 1}: ${t.name}`}
                onClick={() => setIdx(i)}
              />
            ))}
          </div>
        </div>
      </section>

      <button type="button" className="zf banner-live" onClick={onGoLive}>
        <span className="badge badge-pink">Ao vivo · prévia</span>
        <span>Canais de exemplo com grade ilustrativa. Veja a seção Ao vivo.</span>
      </button>

      {continuing.length > 0 && (
        <section className="shelf" aria-label="Continue assistindo">
          <h3>Continue assistindo</h3>
          <div className="shelf-row">
            {continuing.map((t) => (
              <TitleCard key={t.id} title={t} onOpen={onOpenDetails} />
            ))}
          </div>
        </section>
      )}

      <section className="shelf" aria-label="Sua próxima grande história">
        <h3>Sua próxima grande história</h3>
        <div className="shelf-row">
          {TITLES.map((t) => (
            <TitleCard key={t.id} title={t} onOpen={onOpenDetails} />
          ))}
        </div>
      </section>
    </div>
  );
}

interface CatalogProps {
  kind: TitleKind;
  heading: string;
  onOpen: (id: string) => void;
}

export function CatalogView({ kind, heading, onOpen }: CatalogProps) {
  const [query, setQuery] = useState("");
  const [genre, setGenre] = useState<string>("all");
  const pool = useMemo(() => TITLES.filter((t) => t.kind === kind), [kind]);
  const genres = useMemo(() => GENRES.filter((g) => pool.some((t) => t.genre === g)), [pool]);
  const list = pool.filter(
    (t) =>
      (genre === "all" || t.genre === genre) &&
      (query.trim() === "" || t.name.toLowerCase().includes(query.trim().toLowerCase())),
  );

  return (
    <div className="catalog">
      <h2 className="section-title">{heading}</h2>
      <div className="toolbar">
        <input
          className="zf field"
          data-autofocus="true"
          type="search"
          placeholder="Filtrar por nome"
          aria-label="Filtrar por nome"
          value={query}
          onChange={(e) => setQuery(e.target.value)}
        />
        <div className="chip-group" aria-label="Gênero">
          <button type="button" className="zf chip" aria-pressed={genre === "all"} onClick={() => setGenre("all")}>
            Todos
          </button>
          {genres.map((g) => (
            <button key={g} type="button" className="zf chip" aria-pressed={genre === g} onClick={() => setGenre(g)}>
              {g}
            </button>
          ))}
        </div>
      </div>
      {list.length === 0 ? (
        <div className="empty">
          <h3>Nada encontrado</h3>
          <p className="muted">Tente outro nome ou gênero.</p>
        </div>
      ) : (
        <div className="grid">
          {list.map((t) => (
            <TitleCard key={t.id} title={t} onOpen={onOpen} />
          ))}
        </div>
      )}
    </div>
  );
}

interface ListProps {
  favTitles: ZTitle[];
  onOpen: (id: string) => void;
}

export function ListView({ favTitles, onOpen }: ListProps) {
  return (
    <div className="catalog">
      <h2 className="section-title">Minha lista</h2>
      {favTitles.length === 0 ? (
        <div className="empty">
          <h3>Sua lista está vazia</h3>
          <p className="muted">Abra um título e use “Adicionar à lista”. Fica salva neste perfil.</p>
        </div>
      ) : (
        <div className="grid">
          {favTitles.map((t, i) => (
            <TitleCard key={t.id} title={t} onOpen={onOpen} autofocus={i === 0} />
          ))}
        </div>
      )}
    </div>
  );
}

interface SearchProps {
  query: string;
  onQuery: (q: string) => void;
  onOpenTitle: (id: string) => void;
  onOpenChannel: (id: string) => void;
}

export function SearchView({ query, onQuery, onOpenTitle, onOpenChannel }: SearchProps) {
  const q = query.trim().toLowerCase();
  const titles = q ? TITLES.filter((t) => t.name.toLowerCase().includes(q) || t.genre.toLowerCase().includes(q)) : [];
  const channels: ZChannel[] = q ? CHANNELS.filter((c) => c.name.toLowerCase().includes(q)) : [];

  return (
    <div className="catalog">
      <h2 className="section-title">Busca</h2>
      <input
        className="zf field search-input"
        data-autofocus="true"
        type="search"
        placeholder="Filmes, séries ou canais (F2)"
        aria-label="Buscar"
        value={query}
        onChange={(e) => onQuery(e.target.value)}
      />
      {!q && <p className="muted">Digite para buscar no catálogo de exemplo.</p>}
      {q && titles.length === 0 && channels.length === 0 && (
        <div className="empty">
          <h3>Nenhum resultado</h3>
          <p className="muted">Busca feita só no catálogo de demonstração.</p>
        </div>
      )}
      {titles.length > 0 && (
        <>
          <h3 className="results-label">Títulos</h3>
          <div className="grid">
            {titles.map((t) => (
              <TitleCard key={t.id} title={t} onOpen={onOpenTitle} />
            ))}
          </div>
        </>
      )}
      {channels.length > 0 && (
        <>
          <h3 className="results-label">Canais</h3>
          <div className="results-channels">
            {channels.map((c) => (
              <button key={c.id} type="button" className="zf result-channel" onClick={() => onOpenChannel(c.id)}>
                <span className="result-initials" style={gradientStyle(c.colors)}>{c.initials}</span>
                <span>{c.name}</span>
              </button>
            ))}
          </div>
        </>
      )}
    </div>
  );
}

interface SettingsProps {
  settings: Settings;
  onChange: (patch: Partial<Settings>) => void;
  sourceText: string;
  onOpenSource: () => void;
}

export function SettingsView({ settings, onChange, sourceText, onOpenSource }: SettingsProps) {
  const { gamepad } = useTv();
  const padText = gamepad.name
    ? `Controle detectado: ${gamepad.name} (mapeamento não validado)`
    : "Nenhum controle detectado. Conecte um controle e pressione um botão.";

  return (
    <div className="settings">
      <h2 className="section-title">Configurações</h2>
      <div className="settings-grid">
        <section className="panel">
          <h3>Movimento e som</h3>
          <div className="toggle-row">
            <div>
              <strong>Reduzir animações</strong>
              <p className="muted">Respeita também a preferência do sistema.</p>
            </div>
            <button
              type="button"
              role="switch"
              aria-checked={settings.reduceMotion}
              className="zf switch"
              data-on={settings.reduceMotion}
              onClick={() => onChange({ reduceMotion: !settings.reduceMotion })}
            >
              <span className="sr-only">Reduzir animações</span>
            </button>
          </div>
          <div className="toggle-row">
            <div>
              <strong>Sons</strong>
              <p className="muted">Nesta prévia nenhum áudio é tocado. A opção só fica guardada.</p>
            </div>
            <button
              type="button"
              role="switch"
              aria-checked={settings.sounds}
              className="zf switch"
              data-on={settings.sounds}
              onClick={() => onChange({ sounds: !settings.sounds })}
            >
              <span className="sr-only">Sons</span>
            </button>
          </div>
        </section>

        <section className="panel">
          <h3>Fonte</h3>
          <p>{sourceText}</p>
          <p className="muted">Status: demonstração. Nenhuma lista ou serviço é conectado nesta prévia.</p>
          <button type="button" className="zf btn btn-ghost" onClick={onOpenSource}>
            Trocar fonte
          </button>
        </section>

        <section className="panel">
          <h3>Controles</h3>
          <p className="muted">{padText}</p>
          <ol className="steps">
            <li>Setas ou D-pad: mover o foco.</li>
            <li>Enter ou X: selecionar.</li>
            <li>Escape ou O: voltar.</li>
            <li>F2 ou △: busca.</li>
            <li>F3 ou □: configurações.</li>
            <li>PageUp/PageDown ou L1/R1: trocar seção.</li>
          </ol>
        </section>

        <section className="panel">
          <h3>Validação no PS4</h3>
          <p className="form-error">
            Esta prévia roda no navegador. Compatibilidade com PS4, mapeamento do DualShock e desempenho na TV não foram validados.
          </p>
        </section>

        <section className="panel">
          <h3>Sobre a prévia</h3>
          <p className="muted">
            Catálogo, canais e programação são ilustrativos. Não há IPTV, EPG, autenticação ou streaming real.
          </p>
        </section>
      </div>
    </div>
  );
}

interface DetailsProps {
  title: ZTitle;
  inList: boolean;
  onClose: () => void;
  onPlay: (episode?: number) => void;
  onToggleFav: () => void;
}

export function DetailsModal({ title, inList, onClose, onPlay, onToggleFav }: DetailsProps) {
  return (
    <div className="modal-backdrop" data-modal-open="true">
      <section className="modal" aria-label={title.name}>
        <div className="modal-hero">
          <div className="modal-hero-art" style={gradientStyle(title.colors)} aria-hidden="true" />
          <div className="modal-hero-shade" aria-hidden="true" />
          <button type="button" className="zf modal-close" aria-label="Fechar" onClick={onClose}>
            ✕
          </button>
        </div>
        <div className="modal-body">
          <h2 className="hero-title">{title.name}</h2>
          <p className="hero-meta">
            {title.year} · {title.age}+ · {title.duration} · {title.genre}
          </p>
          <p className="hero-synopsis">{title.synopsis}</p>
          {title.kind === "movie" && (
            <div className="hero-actions">
              <button type="button" className="zf btn btn-primary" data-autofocus="true" onClick={() => onPlay()}>
                ▶ Assistir prévia
              </button>
              <button type="button" className="zf btn btn-ghost" onClick={onToggleFav}>
                {inList ? "Remover da lista" : "Adicionar à lista"}
              </button>
            </div>
          )}
          {title.kind === "series" && (
            <>
              <div className="hero-actions">
                <button type="button" className="zf btn btn-ghost" onClick={onToggleFav}>
                  {inList ? "Remover da lista" : "Adicionar à lista"}
                </button>
              </div>
              <h3 className="results-label">Episódios (exemplo)</h3>
              <div className="episode-list">
                {(title.episodes ?? []).map((ep, i) => (
                  <button
                    key={ep.number}
                    type="button"
                    className="zf episode"
                    data-autofocus={i === 0 ? "true" : undefined}
                    onClick={() => onPlay(ep.number)}
                  >
                    <span className="episode-num">{ep.number}</span>
                    <span className="episode-thumb" style={gradientStyle(ep.colors)} aria-hidden="true" />
                    <span className="episode-meta">
                      <strong>{ep.name}</strong>
                      <br />
                      {ep.duration}
                    </span>
                    <span className="episode-play" aria-hidden="true">▶</span>
                  </button>
                ))}
              </div>
            </>
          )}
        </div>
      </section>
    </div>
  );
}

interface PlayerProps {
  title: string;
  subtitle: string;
  colors: [string, string];
  onClose: () => void;
}

export function PlayerView({ title, subtitle, colors, onClose }: PlayerProps) {
  return (
    <section className="player" data-modal-open="true" aria-label={`Prévia: ${title}`}>
      <div className="player-stage">
        <div className="player-art" style={gradientStyle(colors)} aria-hidden="true" />
        <p className="player-note" role="note">
          {NOTE_NO_SIGNAL}
        </p>
      </div>
      <div className="player-bar">
        <h2 className="player-title">
          {title}
          <span className="muted"> · {subtitle}</span>
        </h2>
        <div className="player-progress" aria-hidden="true">
          <span />
        </div>
        <div className="player-controls">
          <div className="fake-controls" aria-hidden="true">
            <span>⏮</span>
            <span>⏯</span>
            <span>⏭</span>
          </div>
          <button type="button" className="zf btn btn-ghost" data-autofocus="true" onClick={onClose}>
            Fechar
          </button>
        </div>
      </div>
    </section>
  );
}
