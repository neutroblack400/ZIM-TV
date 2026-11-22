// Catálogo demonstrativo. Nenhum destes títulos ou canais é transmitido pelo app.
// Metadados são ilustrativos. Artes são gradientes próprios, sem pôsteres reais.

export type TitleKind = "movie" | "series";

export interface Episode {
  number: number;
  name: string;
  duration: string;
  colors: [string, string];
}

export interface ZTitle {
  id: string;
  name: string;
  kind: TitleKind;
  year: number;
  age: number;
  duration: string;
  genre: string;
  synopsis: string;
  colors: [string, string];
  progress: number; // 0–1, ilustrativo
  match: number; // indicador ilustrativo de recomendação
  episodes?: Episode[];
}

const seriesEpisodes = (colors: [string, string][]): Episode[] =>
  ["Início", "Pressão", "Revelação", "Fratura", "Desfecho"].map((name, i) => ({
    number: i + 1,
    name,
    duration: `${44 + i} min`,
    colors: colors[i % colors.length],
  }));

export const TITLES: ZTitle[] = [
  {
    id: "duna-2",
    name: "Duna: Parte Dois",
    kind: "movie",
    year: 2024,
    age: 14,
    duration: "2 h 46 min",
    genre: "Ficção científica",
    synopsis:
      "Paul Atreides se une aos Fremen e enfrenta escolhas que podem mudar o destino de um universo inteiro.",
    colors: ["#c46b2a", "#2d1550"],
    progress: 0.45,
    match: 97,
  },
  {
    id: "ruptura",
    name: "Ruptura",
    kind: "series",
    year: 2022,
    age: 16,
    duration: "Séries · exemplo",
    genre: "Suspense",
    synopsis:
      "Funcionários têm a memória dividida entre a vida no trabalho e a vida fora dele, e uma descoberta põe tudo em xeque.",
    colors: ["#1f6f78", "#100921"],
    progress: 0.3,
    match: 94,
    episodes: seriesEpisodes([
      ["#1f6f78", "#100921"],
      ["#2b8a94", "#1a1030"],
    ]),
  },
  {
    id: "tlou",
    name: "The Last of Us",
    kind: "series",
    year: 2023,
    age: 16,
    duration: "Séries · exemplo",
    genre: "Drama",
    synopsis:
      "Depois de um colapso global, um sobrevivente cínico é contratado para levar uma jovem por um país devastado.",
    colors: ["#3f5a2a", "#1a0f2e"],
    progress: 0.8,
    match: 92,
    episodes: seriesEpisodes([
      ["#3f5a2a", "#1a0f2e"],
      ["#56703a", "#241540"],
    ]),
  },
  {
    id: "interestelar",
    name: "Interestelar",
    kind: "movie",
    year: 2014,
    age: 12,
    duration: "2 h 49 min",
    genre: "Aventura",
    synopsis:
      "Uma equipe atravessa um buraco de minhoca em busca de um novo lar para a humanidade.",
    colors: ["#1b2a6b", "#7942ec"],
    progress: 0,
    match: 95,
  },
  {
    id: "shogun",
    name: "Shōgun",
    kind: "series",
    year: 2024,
    age: 16,
    duration: "Séries · exemplo",
    genre: "Histórico",
    synopsis:
      "No Japão do século XVII, um navegador europeu se vê no centro de uma disputa de poder entre senhores feudais.",
    colors: ["#8b2d4d", "#2a0f3f"],
    progress: 0,
    match: 90,
    episodes: seriesEpisodes([
      ["#8b2d4d", "#2a0f3f"],
      ["#a33d5e", "#331450"],
    ]),
  },
  {
    id: "the-batman",
    name: "The Batman",
    kind: "movie",
    year: 2022,
    age: 14,
    duration: "2 h 56 min",
    genre: "Suspense",
    synopsis:
      "Nos primeiros anos como justiceiro, Bruce Wayne investiga uma série de crimes que expõem segredos de Gotham.",
    colors: ["#0e0e16", "#5a3aa0"],
    progress: 0.12,
    match: 89,
  },
  {
    id: "mare-noturna",
    name: "Maré Noturna",
    kind: "movie",
    year: 2025,
    age: 14,
    duration: "1 h 52 min",
    genre: "Terror",
    synopsis: "Uma vila costeira começa a ouvir vozes no mar depois de cada tempestade.",
    colors: ["#0f3d4a", "#06121f"],
    progress: 0,
    match: 88,
  },
  {
    id: "cidade-de-vidro",
    name: "Cidade de Vidro",
    kind: "series",
    year: 2025,
    age: 16,
    duration: "Séries · exemplo",
    genre: "Thriller",
    synopsis: "Uma detetive descobre que os crimes da cidade seguem um padrão escrito por alguém que ela conhece.",
    colors: ["#4b1d6b", "#0a0616"],
    progress: 0,
    match: 91,
    episodes: seriesEpisodes([
      ["#4b1d6b", "#0a0616"],
      ["#5f2a84", "#140a24"],
    ]),
  },
  {
    id: "quintal-de-estrelas",
    name: "Quintal de Estrelas",
    kind: "movie",
    year: 2023,
    age: 0,
    duration: "1 h 34 min",
    genre: "Animação",
    synopsis: "Um robô doméstico e uma menina saem numa missão para devolver um telescópio roubado do quintal.",
    colors: ["#e8762b", "#f3c46a"],
    progress: 0,
    match: 93,
  },
  {
    id: "o-ultimo-verao",
    name: "O Último Verão",
    kind: "movie",
    year: 2024,
    age: 12,
    duration: "1 h 58 min",
    genre: "Romance",
    synopsis: "Dois amigos de infância se reencontram numa cidade de praia no verão em que tudo vai mudar.",
    colors: ["#c2407a", "#3a1450"],
    progress: 0.6,
    match: 86,
  },
  {
    id: "rota-zero",
    name: "Rota Zero",
    kind: "movie",
    year: 2025,
    age: 14,
    duration: "2 h 05 min",
    genre: "Ação",
    synopsis: "Uma piloto de corrida precisa atravessar o deserto em 48 horas para salvar a equipe.",
    colors: ["#b5651d", "#1a0f2e"],
    progress: 0,
    match: 85,
  },
  {
    id: "sem-roteiro",
    name: "Sem Roteiro",
    kind: "series",
    year: 2024,
    age: 12,
    duration: "Séries · exemplo",
    genre: "Comédia",
    synopsis: "Uma equipe de roteiristas tenta terminar uma série enquanto o próprio elenco reescreve tudo.",
    colors: ["#f5a3d8", "#5b2a7a"],
    progress: 0,
    match: 87,
    episodes: seriesEpisodes([
      ["#f5a3d8", "#5b2a7a"],
      ["#e88cc6", "#3d1c57"],
    ]),
  },
  {
    id: "o-jardineiro",
    name: "O Jardineiro",
    kind: "movie",
    year: 2021,
    age: 12,
    duration: "1 h 41 min",
    genre: "Documentário",
    synopsis: "Um homem cultiva a mesma árvore há quarenta anos e conta o que ela viu nesse tempo.",
    colors: ["#2f5d3a", "#0e1a10"],
    progress: 0,
    match: 82,
  },
  {
    id: "sinal-perdido",
    name: "Sinal Perdido",
    kind: "series",
    year: 2023,
    age: 16,
    duration: "Séries · exemplo",
    genre: "Terror",
    synopsis: "Um programa de rádio noturno recebe chamadas de uma voz que diz morar na casa do apresentador.",
    colors: ["#2a1140", "#050308"],
    progress: 0,
    match: 84,
    episodes: seriesEpisodes([
      ["#2a1140", "#050308"],
      ["#3a1a52", "#0a0610"],
    ]),
  },
];

export const GENRES = Array.from(new Set(TITLES.map((t) => t.genre))).sort();

export type CategoryId =
  | "favorites"
  | "all"
  | "variedades"
  | "abertos"
  | "filmes"
  | "docs"
  | "legendados"
  | "noticias"
  | "esportes"
  | "ppv"
  | "infantil";

export interface ZCategory {
  id: CategoryId;
  label: string;
  tag: string; // identificação curta colorida
  color: string;
}

export const CATEGORIES: ZCategory[] = [
  { id: "favorites", label: "Favoritos", tag: "★", color: "#f577d2" },
  { id: "all", label: "Todos os canais", tag: "TO", color: "#c9b6f2" },
  { id: "variedades", label: "Variedades", tag: "VA", color: "#c1f873" },
  { id: "abertos", label: "Canais abertos", tag: "AB", color: "#7942ec" },
  { id: "filmes", label: "Filmes e séries", tag: "FI", color: "#f577d2" },
  { id: "docs", label: "Documentários", tag: "DO", color: "#5ec8ff" },
  { id: "legendados", label: "Legendados", tag: "LE", color: "#ffb36b" },
  { id: "noticias", label: "Notícias", tag: "NO", color: "#ff6b8a" },
  { id: "esportes", label: "Esportes", tag: "ES", color: "#7ee0a0" },
  { id: "ppv", label: "PPV", tag: "PP", color: "#e0b0ff" },
  { id: "infantil", label: "Infantil", tag: "IN", color: "#ffe066" },
];

export interface ZChannel {
  id: string;
  name: string;
  initials: string;
  cats: CategoryId[];
  colors: [string, string];
  now: string;
  nowDesc: string;
  next: string;
}

// Canais de exemplo. Nomes e programas são ilustrativos.
export const CHANNELS: ZChannel[] = [
  { id: "globo-sp", name: "Globo SP", initials: "GL", cats: ["abertos", "variedades"], colors: ["#0b4f9c", "#1d6fd0"], now: "Programa de variedades", nowDesc: "Exemplo de programação ao vivo para demonstração.", next: "Jornal regional (exemplo)" },
  { id: "sbt", name: "SBT", initials: "SB", cats: ["abertos", "variedades"], colors: ["#e8762b", "#f3a24a"], now: "Programa de auditório", nowDesc: "Descrição ilustrativa do programa.", next: "Telejornal (exemplo)" },
  { id: "record", name: "Record", initials: "RC", cats: ["abertos", "variedades"], colors: ["#0a2a6e", "#1e4bb5"], now: "Novela (exemplo)", nowDesc: "Descrição ilustrativa do programa.", next: "Jornal da noite (exemplo)" },
  { id: "band", name: "Band", initials: "BD", cats: ["abertos", "noticias"], colors: ["#111111", "#444444"], now: "Bloco de notícias", nowDesc: "Descrição ilustrativa do programa.", next: "Esportes (exemplo)" },
  { id: "sportv", name: "SporTV", initials: "SP", cats: ["esportes"], colors: ["#0e7a3a", "#1fb55a"], now: "Jogo ao vivo (exemplo)", nowDesc: "Transmissão esportiva ilustrativa.", next: "Análise da rodada (exemplo)" },
  { id: "espn", name: "ESPN", initials: "ES", cats: ["esportes"], colors: ["#b3142a", "#e0334a"], now: "Debate esportivo (exemplo)", nowDesc: "Descrição ilustrativa do programa.", next: "Highlights (exemplo)" },
  { id: "premiere", name: "Premiere", initials: "PR", cats: ["filmes", "esportes"], colors: ["#2a0a4a", "#5b1a8a"], now: "Filme em exibição (exemplo)", nowDesc: "Descrição ilustrativa do programa.", next: "Série (exemplo)" },
  { id: "hbo", name: "HBO", initials: "HB", cats: ["filmes", "legendados"], colors: ["#1a1a1a", "#4a1d6e"], now: "Série em exibição (exemplo)", nowDesc: "Descrição ilustrativa do programa.", next: "Filme (exemplo)" },
  { id: "ae", name: "A&E", initials: "AE", cats: ["docs"], colors: ["#222222", "#666666"], now: "Documentário (exemplo)", nowDesc: "Descrição ilustrativa do programa.", next: "Investigação (exemplo)" },
  { id: "discovery", name: "Discovery", initials: "DS", cats: ["docs"], colors: ["#0b5fa5", "#2aa0e0"], now: "Documentário (exemplo)", nowDesc: "Descrição ilustrativa do programa.", next: "Ciência (exemplo)" },
  { id: "natgeo", name: "National Geographic", initials: "NG", cats: ["docs", "legendados"], colors: ["#c9a227", "#6b5210"], now: "Natureza (exemplo)", nowDesc: "Descrição ilustrativa do programa.", next: "Expedição (exemplo)" },
  { id: "cnn", name: "CNN Brasil", initials: "CN", cats: ["noticias"], colors: ["#a0102a", "#d2213c"], now: "Jornal ao vivo (exemplo)", nowDesc: "Descrição ilustrativa do programa.", next: "Análise política (exemplo)" },
  { id: "globonews", name: "GloboNews", initials: "GN", cats: ["noticias"], colors: ["#0b2f6b", "#1b57b5"], now: "Painel de notícias (exemplo)", nowDesc: "Descrição ilustrativa do programa.", next: "Economia (exemplo)" },
  { id: "cinema-leg", name: "Cinema Legendado", initials: "CL", cats: ["filmes", "legendados"], colors: ["#3a1466", "#7942ec"], now: "Filme legendado (exemplo)", nowDesc: "Descrição ilustrativa do programa.", next: "Filme legendado (exemplo)" },
  { id: "tv-kids", name: "TV Kids", initials: "TK", cats: ["infantil"], colors: ["#f5c542", "#f577d2"], now: "Desenho animado (exemplo)", nowDesc: "Descrição ilustrativa do programa.", next: "Programa infantil (exemplo)" },
];

export function countFor(cat: CategoryId, favChannelIds: string[]): number {
  if (cat === "favorites") return favChannelIds.length;
  if (cat === "all") return CHANNELS.length;
  return CHANNELS.filter((c) => c.cats.includes(cat)).length;
}

export function channelsFor(cat: CategoryId, favChannelIds: string[]): ZChannel[] {
  if (cat === "favorites") return CHANNELS.filter((c) => favChannelIds.includes(c.id));
  if (cat === "all") return CHANNELS;
  return CHANNELS.filter((c) => c.cats.includes(cat));
}

export function titleById(id: string): ZTitle | undefined {
  return TITLES.find((t) => t.id === id);
}
