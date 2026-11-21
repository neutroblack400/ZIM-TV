import { useMemo, useState } from "react";
import { CATEGORIES, CHANNELS, channelsFor, countFor, type CategoryId, type ZChannel } from "./zim-data";
import { gradientStyle, useClock, formatTime } from "./zim-ui";

// Ao vivo: três painéis (categorias, canais com busca e favoritos, prévia). Dados só de demonstração.

interface LiveProps {
  favChannels: string[];
  onToggleFav: (id: string) => void;
  onOpenChannel: (id: string) => void;
}

function slotLabel(d: Date): string {
  const h = d.getHours();
  const pad = (n: number) => String(n).padStart(2, "0");
  return `${pad(h)}:00 – ${pad((h + 1) % 24)}:00`;
}

export function LiveTv({ favChannels, onToggleFav, onOpenChannel }: LiveProps) {
  const now = useClock();
  const [cat, setCat] = useState<CategoryId>("all");
  const [query, setQuery] = useState("");
  const [selectedId, setSelectedId] = useState<string | null>(null);

  const list = useMemo(() => {
    const q = query.trim().toLowerCase();
    return channelsFor(cat, favChannels).filter((c) => !q || c.name.toLowerCase().includes(q));
  }, [cat, query, favChannels]);

  const selected: ZChannel | undefined =
    list.find((c) => c.id === selectedId) ?? list[0] ?? undefined;
  const isFav = (id: string) => favChannels.includes(id);

  return (
    <div className="live">
      <div className="live-clock">
        <span className="clock">{formatTime(now)}</span>
        <span className="muted">Programação ilustrativa · EPG não conectado</span>
      </div>
      <div className="live-grid">
        <section className="live-panel" aria-label="Categorias">
          <h3>Categorias</h3>
          <div className="live-cats">
            {CATEGORIES.map((c) => (
              <button
                key={c.id}
                type="button"
                className="zf live-cat"
                aria-pressed={cat === c.id}
                data-autofocus={cat === c.id ? "true" : undefined}
                onClick={() => setCat(c.id)}
              >
                <span className="live-cat-tag" style={{ background: c.color }} aria-hidden="true">
                  {c.tag}
                </span>
                <span className="live-cat-label">{c.label}</span>
                <span className="live-cat-count">{countFor(c.id, favChannels)}</span>
              </button>
            ))}
          </div>
        </section>

        <section className="live-panel" aria-label="Canais">
          <h3>Canais</h3>
          <input
            className="zf field"
            type="search"
            placeholder="Buscar canal"
            aria-label="Buscar canal"
            value={query}
            onChange={(e) => setQuery(e.target.value)}
          />
          {list.length === 0 ? (
            <p className="muted live-empty">
              {cat === "favorites" ? "Nenhum favorito ainda. Use ★ para marcar canais." : "Nenhum canal encontrado."}
            </p>
          ) : (
            <ul className="live-list">
              {list.map((c) => (
                <li key={c.id} className="live-row">
                  <button
                    type="button"
                    className="zf live-channel"
                    aria-current={selected?.id === c.id ? "true" : undefined}
                    onFocus={() => setSelectedId(c.id)}
                    onClick={() => onOpenChannel(c.id)}
                  >
                    <span className="live-initials" style={gradientStyle(c.colors)} aria-hidden="true">
                      {c.initials}
                    </span>
                    <span className="live-name">{c.name}</span>
                  </button>
                  <button
                    type="button"
                    className="zf live-fav"
                    aria-pressed={isFav(c.id)}
                    aria-label={`${isFav(c.id) ? "Remover" : "Adicionar"} ${c.name} dos favoritos`}
                    onClick={() => onToggleFav(c.id)}
                  >
                    {isFav(c.id) ? "★" : "☆"}
                  </button>
                </li>
              ))}
            </ul>
          )}
        </section>

        <section className="live-panel live-preview" aria-label="Prévia do canal">
          {selected ? (
            <>
              <span className="badge badge-pink">Ao vivo · prévia</span>
              <div className="live-art" style={gradientStyle(selected.colors)} aria-hidden="true">
                <span>{selected.initials}</span>
              </div>
              <h3>{selected.name}</h3>
              <p className="live-slot">{slotLabel(now)}</p>
              <p>
                <strong>Agora:</strong> {selected.now}
              </p>
              <p className="muted">{selected.nowDesc}</p>
              <p>
                <strong>Depois:</strong> {selected.next}
              </p>
              <p className="disclaimer">Programação ilustrativa · EPG não conectado</p>
              <button type="button" className="zf btn btn-primary" onClick={() => onOpenChannel(selected.id)}>
                Abrir prévia do canal
              </button>
            </>
          ) : (
            <p className="muted">Escolha um canal para ver a prévia.</p>
          )}
        </section>
      </div>
      <p className="muted live-counts">
        {CHANNELS.length} canais de exemplo no total. Contagens vêm só dos dados de demonstração.
      </p>
    </div>
  );
}
