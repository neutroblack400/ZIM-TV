import { useEffect, useMemo, useState, type ReactNode } from "react";
import "./tv-shell.css";
import "./zim.css";
import "./zim-entry.css";
import "./zim-live.css";
import { TvShell, type Action } from "./tv-shell";
import {
  DEMO_SOURCE,
  MAX_PROFILES,
  loadProfiles,
  loadSettings,
  makeProfile,
  saveProfiles,
  saveSettings,
  type Profile,
  type Settings,
  type SourceConfig,
  type SourceKind,
  type StoredProfiles,
} from "./zim-store";
import { CHANNELS, TITLES, titleById } from "./zim-data";
import { SECTIONS, sourceLabel, type Section } from "./zim-ui";
import {
  LoadingScreen,
  ProfileEditor,
  ProfilesScreen,
  SourceForm,
  SourceScreen,
} from "./zim-entry";
import { CatalogView, DashHeader, DetailsModal, HomeView, ListView, PlayerView, SearchView, SettingsView } from "./zim-dashboard";
import { LiveTv } from "./zim-live";

// Máquina de telas da prévia: perfis → fonte → formulário → carregamento → painel.
// Tudo é demonstrativo: nenhuma lista, EPG, login ou stream é conectado.

type Screen = "profiles" | "source" | "xtream" | "m3u" | "loading" | "app";

type PlayerState =
  | { kind: "title"; titleId: string; episode?: number }
  | { kind: "channel"; channelId: string }
  | null;

function toggle(list: string[], id: string): string[] {
  return list.includes(id) ? list.filter((x) => x !== id) : [...list, id];
}

export function ZimApp() {
  const [data, setData] = useState<StoredProfiles>(() => loadProfiles());
  const [settings, setSettings] = useState<Settings>(() => loadSettings());
  const [screen, setScreen] = useState<Screen>("profiles");
  const [sourceFrom, setSourceFrom] = useState<"entry" | "app">("entry");
  const [manage, setManage] = useState(false);
  const [editor, setEditor] = useState<{ id: string | null } | null>(null);
  const [pending, setPending] = useState<SourceConfig | null>(null);
  const [attempt, setAttempt] = useState(0);
  const [section, setSection] = useState<Section>("home");
  const [detailsId, setDetailsId] = useState<string | null>(null);
  const [player, setPlayer] = useState<PlayerState>(null);
  const [query, setQuery] = useState("");

  useEffect(() => saveProfiles(data), [data]);
  useEffect(() => saveSettings(settings), [settings]);

  useEffect(() => {
    document.title = "ZIMTV · Prévia";
  }, []);

  const active: Profile | null = useMemo(
    () => data.profiles.find((p) => p.id === data.activeId) ?? null,
    [data],
  );

  const updateProfiles = (fn: (list: Profile[]) => Profile[]) =>
    setData((d) => ({ ...d, profiles: fn(d.profiles) }));

  const updateActive = (fn: (p: Profile) => Profile) => {
    if (!active) return;
    updateProfiles((list) => list.map((p) => (p.id === active.id ? fn(p) : p)));
  };

  const patchSettings = (patch: Partial<Settings>) => setSettings((s) => ({ ...s, ...patch }));

  const goProfiles = () => {
    setData((d) => ({ ...d, activeId: null }));
    setScreen("profiles");
    setSection("home");
    setDetailsId(null);
    setPlayer(null);
  };

  const openSource = (from: "entry" | "app") => {
    setSourceFrom(from);
    setDetailsId(null);
    setPlayer(null);
    setScreen("source");
  };

  const chooseKind = (kind: SourceKind) => {
    if (kind === "demo") {
      setPending({ ...DEMO_SOURCE });
      setAttempt((n) => n + 1);
      setScreen("loading");
      return;
    }
    setScreen(kind);
  };

  const finishLoading = () => {
    if (active && pending) {
      updateActive((p) => ({ ...p, source: pending }));
    }
    setPending(null);
    setSection("home");
    setDetailsId(null);
    setPlayer(null);
    setScreen("app");
  };

  const sourceFormSubmit = (source: SourceConfig) => {
    setPending(source);
    setAttempt((n) => n + 1);
    setScreen("loading");
  };

  // Ordem de voltar: player → editor → detalhes → seção/tela anterior.
  const back = () => {
    if (player) return setPlayer(null);
    if (editor) return setEditor(null);
    if (detailsId) return setDetailsId(null);
    switch (screen) {
      case "app":
        if (section !== "home") return setSection("home");
        return goProfiles();
      case "source":
        if (sourceFrom === "app") return setScreen("app");
        return goProfiles();
      case "xtream":
      case "m3u":
      case "loading":
        return setScreen("source");
      default:
        return;
    }
  };

  const modalOpen = Boolean(player || editor || detailsId);

  const onAction = (action: Action) => {
    if (action === "back") return back();
    if (screen !== "app" || modalOpen) return;
    if (action === "search") return setSection("search");
    if (action === "settings") return setSection("settings");
    if (action === "prevSection" || action === "nextSection") {
      const i = SECTIONS.indexOf(section);
      const step = action === "nextSection" ? 1 : -1;
      setSection(SECTIONS[(i + step + SECTIONS.length) % SECTIONS.length]);
      setDetailsId(null);
    }
  };

  const saveEditor = (name: string, color: string) => {
    if (!editor) return;
    if (editor.id === null) {
      if (data.profiles.length >= MAX_PROFILES) return;
      updateProfiles((list) => [...list, makeProfile(name, color)]);
    } else {
      const id = editor.id;
      updateProfiles((list) => list.map((p) => (p.id === id ? { ...p, name, color } : p)));
    }
    setEditor(null);
  };

  const deleteEditing = () => {
    if (!editor?.id || data.profiles.length <= 1) return;
    const id = editor.id;
    updateProfiles((list) => list.filter((p) => p.id !== id));
    setData((d) => (d.activeId === id ? { ...d, activeId: null } : d));
    setEditor(null);
  };

  const editingProfile = editor?.id ? data.profiles.find((p) => p.id === editor.id) ?? null : null;

  const favTitles = active?.favTitles ?? [];
  const favChannels = active?.favChannels ?? [];

  const currentTitle = detailsId ? titleById(detailsId) : undefined;
  const playerProps = (() => {
    if (!player) return null;
    if (player.kind === "channel") {
      const c = CHANNELS.find((x) => x.id === player.channelId);
      if (!c) return null;
      return { title: c.name, subtitle: "Ao vivo · prévia", colors: c.colors };
    }
    const t = titleById(player.titleId);
    if (!t) return null;
    const ep = player.episode !== undefined ? t.episodes?.find((e) => e.number === player.episode) : undefined;
    return {
      title: ep ? `${t.name} · Ep. ${ep.number}` : t.name,
      subtitle: ep ? `${ep.name} · demonstração` : t.kind === "series" ? "Série · demonstração" : "Filme · demonstração",
      colors: ep ? ep.colors : t.colors,
    };
  })();

  const focusKey = [
    screen,
    section,
    detailsId ?? "",
    player ? `${player.kind}:${player.kind === "channel" ? player.channelId : player.titleId}:${player.kind === "title" ? player.episode ?? "" : ""}` : "",
    editor ? `editor:${editor.id ?? "new"}` : "",
    manage ? "manage" : "",
    attempt,
    sourceFrom,
  ].join("|");

  const tvMode = settings.tvMode;

  let content: ReactNode;
  if (!active || screen === "profiles") {
    content = (
      <ProfilesScreen
        profiles={data.profiles}
        manage={manage}
        onSelect={(id) => {
          setData((d) => ({ ...d, activeId: id }));
          openSource("entry");
        }}
        onEdit={(id) => setEditor({ id })}
        onAdd={() => {
          if (data.profiles.length < MAX_PROFILES) setEditor({ id: null });
        }}
        onToggleManage={() => setManage((m) => !m)}
      />
    );
  } else if (screen === "source") {
    content = <SourceScreen profile={active} onChoose={chooseKind} onBack={back} />;
  } else if (screen === "xtream" || screen === "m3u") {
    content = <SourceForm kind={screen} onSubmit={sourceFormSubmit} onBack={() => setScreen("source")} />;
  } else if (screen === "loading" && pending) {
    content = (
      <LoadingScreen
        key={attempt}
        profile={active}
        source={pending}
        onDone={finishLoading}
        onCancel={() => setScreen("source")}
        onRetry={() => setAttempt((n) => n + 1)}
      />
    );
  } else {
    content = (
      <div className="dash">
        <DashHeader
          section={section}
          onSection={(s) => {
            setSection(s);
            setDetailsId(null);
          }}
          profile={active}
          onSwitchProfile={goProfiles}
          onOpenSource={() => openSource("app")}
        />
        <main className="dash-main">
          {section === "home" && (
            <HomeView
              onOpenDetails={(id) => setDetailsId(id)}
              onPlay={(id) => setPlayer({ kind: "title", titleId: id })}
              onGoLive={() => setSection("live")}
            />
          )}
          {section === "live" && (
            <LiveTv
              favChannels={favChannels}
              onToggleFav={(id) => updateActive((p) => ({ ...p, favChannels: toggle(p.favChannels, id) }))}
              onOpenChannel={(id) => setPlayer({ kind: "channel", channelId: id })}
            />
          )}
          {section === "movies" && <CatalogView key="movies" kind="movie" heading="Filmes" onOpen={(id) => setDetailsId(id)} />}
          {section === "series" && <CatalogView key="series" kind="series" heading="Séries" onOpen={(id) => setDetailsId(id)} />}
          {section === "list" && (
            <ListView
              favTitles={TITLES.filter((t) => favTitles.includes(t.id))}
              onOpen={(id) => setDetailsId(id)}
            />
          )}
          {section === "search" && (
            <SearchView
              query={query}
              onQuery={setQuery}
              onOpenTitle={(id) => setDetailsId(id)}
              onOpenChannel={(id) => setPlayer({ kind: "channel", channelId: id })}
            />
          )}
          {section === "settings" && (
            <SettingsView
              settings={settings}
              onChange={patchSettings}
              sourceText={`Fonte atual: ${sourceLabel(active.source)}`}
              onOpenSource={() => openSource("app")}
            />
          )}
        </main>

        {currentTitle && !player && (
          <DetailsModal
            key={currentTitle.id}
            title={currentTitle}
            inList={favTitles.includes(currentTitle.id)}
            onClose={() => setDetailsId(null)}
            onPlay={(ep) => setPlayer({ kind: "title", titleId: currentTitle.id, episode: ep })}
            onToggleFav={() => updateActive((p) => ({ ...p, favTitles: toggle(p.favTitles, currentTitle.id) }))}
          />
        )}
        {playerProps && (
          <PlayerView {...playerProps} onClose={() => setPlayer(null)} />
        )}
      </div>
    );
  }

  return (
    <TvShell
      tvMode={tvMode}
      reduceMotion={settings.reduceMotion}
      onToggleTv={() => patchSettings({ tvMode: !tvMode })}
      onAction={onAction}
      focusKey={focusKey}
    >
      {content}
      {editor && (
        <ProfileEditor
          key={editor.id ?? "new"}
          profile={editingProfile}
          canDelete={data.profiles.length > 1}
          onSave={saveEditor}
          onDelete={deleteEditing}
          onClose={() => setEditor(null)}
        />
      )}
    </TvShell>
  );
}

