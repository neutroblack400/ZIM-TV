# ZIM TV — ESPECIFICAÇÃO-MESTRE (v1.0 — 2026-11-19)

> **Este documento é a referência normativa do projeto.** Ele consolida identidade, requisitos,
> regras de ouro, arquitetura preservada, estágios e critérios de aceite. Toda etapa deve ser
> executada contra ele — sem remover funcionalidades existentes, sem inventar comportamento e sem
> declarar "pronto" sem validação no PS4 real.
>
> Base atual: **CORRIGIDO8-TEST** (perfis com limite de 10 + cache persistente do catálogo).
> PKG oficial `ZIMTV-IPTV-PS4.pkg`: **intocado**.

---

## 1. Identidade e princípios

1. **Produto**: aplicativo IPTV nativo para PS4, completo, bonito, rápido, organizado e confiável.
2. **Identidade visual**: preto e violeta, marca ZIM TV, aparência premium inspirada na fluidez da
   Apple TV. Nada de telas desnecessárias nem informação duplicada.
3. **Nativo**: C++ + SDL2 + libcross2d (adaptada ao PS4) + OpenGL ES 2.0. **Sem HTML/WebView**,
   sem frameworks de UI novos, sem substituir componentes que já funcionam.
4. **Controle**: navegação simples e imediata pelo DualShock 4. Foco responde sem atraso perceptível.
5. **Sem servidor próprio**: o app não depende de infraestrutura do ZIM TV; fala direto com a fonte
   do usuário (Xtream / M3U / EPG).
6. **Idioma padrão**: português; 7 idiomas sincronizados (pt, en, tr, es, fr, it, de).
7. **Animações**: leves, suaves, nunca sacrificando estabilidade ou navegação.

## 2. Regras de ouro (non-negotiáveis)

- **RO-1** Nunca modificar o PKG oficial `ZIMTV-IPTV-PS4.pkg`. Builds saem apenas como
  `ZIMTV-IPTV-PS4-CORRIGIDO<N>-TEST.pkg`.
- **RO-2** Entrega por build: PKG + SOURCE.zip + CHANGELOG + README + CHECKSUMS (+ relatório técnico)
  em `Pkg novo/`, com cópia do PKG e do SOURCE em `PKG PERTO/`. SOURCE.zip sempre corresponde ao PKG.
- **RO-3** Gate de entrega: `pkg_validate` = **28/28 [OK]** e guard de credenciais = **clean**.
- **RO-4** TLS: verificação de certificados **sempre ligada**. Nunca desativar/baixar a guarda para
  "resolver" um erro de certificado.
- **RO-5** Nunca inventar pôsteres/logos. Fallback elegante (placeholder com iniciais/título) só quando
  a imagem realmente não existir.
- **RO-6** Nunca declarar funcionalidade concluída sem **teste no PS4 real**. Compilar + 28/28 não é
  evidência de comportamento.
- **RO-7** Não remover funcionalidades existentes que funcionam (Xtream, M3U/M3U8, XMLTV, player,
  favoritos, histórico, downloads, configurações).
- **RO-8** Tipos separados: canal ≠ filme ≠ série. Circle nunca fecha o app (volta à seleção de
  usuários); X confirma; opções em menu próprio.
- **RO-9** Não misturar dados entre perfis. Cada perfil: fonte, catálogo, cache, favoritos, histórico,
  progresso, EPG e preferências próprios.
- **RO-10** Cache invalível/danificado nunca derruba o app: cair no caminho completo (lento, correto).

## 3. Arquitetura preservada (componentes e arquivos)

| Componente | Onde vive | Estado |
|---|---|---|
| UI / render | `src/ui/*`, `src/screens/*`, libcross2d PS4 (`external/pplay-reference/libcross2d`) | Ativa, sem framework novo |
| Fontes (Xtream/M3U/Stalker-backend) | `src/app/xtream_service.cpp`, `m3u_service.cpp`, `stalker_service.cpp` | Ativas; Stalker fora da UI |
| Parser M3U + classificação | `src/iptv/m3u.{h,cpp}`, `classify.*` | Ativo |
| Catálogos + índices + busca | `src/iptv/catalog.{h,cpp}`, `search_index.*`, `sorting.*` | Ativo |
| Cache do catálogo processado | `src/storage/catalog_store.{h,cpp}` (CORRIGIDO8) | Ativo |
| Cache da lista bruta | `src/storage/catalog_cache.{h,cpp}` | Ativo |
| Perfis / persistência | `src/storage/profile_store.{h,cpp}` (teto 10 no CORRIGIDO8) | Ativo |
| Biblioteca por perfil (favoritos/histórico/progresso) | `src/storage/library_store.{h,cpp}` | Ativo |
| Imagens (pipeline, disco, TMDB) | `src/images/*` (`tmdb_client`, `image_pipeline`, `disk_cache`, `hero_art`) | Ativo; TMDB bloqueado por TLS |
| EPG (XMLTV) | `src/app/epg_service.{h,cpp}`, `src/epg/xmltv.*` | Parser próprio, streaming, cache binário |
| Player | `src/app/playback.*`, mpv 0.34.1 + FFmpeg | Ativo |
| HTTP/TLS | `src/network/http.*`, libcurl 7.80.0 + mbedTLS 2.16.6 (sysroot PacBrew) | **RSA pendente** |
| Stack TLS alternativa | `toolchain/tls-stack/ps4` = mbedTLS **2.28.10** + libcurl 7.80.0 (built p/ PS4) | **Nunca linkada num PKG** |
| Jobs (background) | `src/network/jobs.*` (4 workers) | Ativo |
| i18n | `src/i18n/strings_{en,pt,tr,es,fr,it,de}.cpp` | 7 línguas |

### Decisões de bibliotecas (pasta `OLHAS ISSO` — 82 MB)
Avaliadas e **recusadas** para integração: `RmlUi-6.3`, `imgui-1.93.0`, `json-3.12.0`,
`stb-master`, `libcross2d-1.0`. Motivos: o app já usa libcross2d (versão PS4, mais nova que a da
pasta), stb (via skeleton do cross2d) e parser JSON próprio; imgui/RmlUi acrescentariam uma segunda
stack de UI; nenhuma delas resolve TLS/RSA, TMDB ou EPG. **Não integram nada da pasta.**

---

## 4. Especificação funcional

### 4.1 Usuários e perfis (USR)
- **USR-01** Primeira tela = seleção de usuários ("Quem está assistindo?"), perfis centralizados, `+`.
- **USR-02** Limite de **10 perfis**, validado na lógica (`ProfilesStore::upsert`) e na UI (`+` some
  no 10º; mensagem "Limite máximo de 10 usuários atingido"; excluir libera vaga).
- **USR-03** Criar perfil: nome + avatar personalizável. *(nome: OK; avatar: inicial automática —
  avatar escolhível **pendente**).*
- **USR-04** Editar nome e excluir (com confirmação). *(OK)*
- **USR-05** Fonte do perfil: **Xtream Codes · Lista M3U/M3U8 · EPG XMLTV** (Stalker/MAG fora da UI).
  *(OK)*
- **USR-06** Se o perfil já tem lista carregada, X entra direto no conteúdo (sem reconfigurar).
  *(OK via cache da lista; ganho de velocidade do CORRIGIDO8 a validar no PS4).*
- **USR-07** Dados independentes por perfil: fonte, catálogo, cache, favoritos, histórico, progresso,
  EPG, preferências. *(OK)*
- **USR-08** Perfis sobrevivem a fechar o app e a reiniciar o PS4. *(OK)*

### 4.2 Navegação e Home (NAV)
- **NAV-01** Cabeçalho só: logo ZIM TV · Início · TV ao vivo · Filmes · Séries · relógio. Sem ícones de
  atalho. *(OK)*
- **NAV-02** Transições suaves entre as 4 seções; foco imediato; sem piscar. *(parcial — animações
  Apple TV são a Etapa 5)*
- **NAV-03** Hero cinematográfico com arte do conteúdo; rails (Continue Watching, Recentes,
  Favoritos). *(OK no CORRIGIDO6/7; depende de pôsteres)*
- **NAV-04** Circle = seleção de usuários (nunca fecha o app). Triângulo = pesquisa. *(OK)*

### 4.3 TV ao vivo (LIV)
- **LIV-01** Logos verdadeiros dos canais (tvg-logo). *(OK; logo do provedor morto cai no placeholder)*
- **LIV-02** Categorias organizadas; canais nunca misturados com filmes/séries. *(OK)*
- **LIV-03** Pesquisa e favoritos. *(OK)*
- **LIV-04** EPG: programa atual e próximo por canal (quando houver fonte). *(parser OK; **teste com
  XMLTV real pendente**)*
- **LIV-05** Informações do canal; troca rápida (zap); X abre o player **direto**, sem tela
  intermediária de logo. *(OK no CORRIGIDO7)*
- **LIV-06** Guia de programação navegável. *(parcial — validar com EPG real)*

### 4.4 Filmes (MOV)
- **MOV-01** Catálogo exclusivo de filmes; pôster oficial por título/ano/tipo; **nunca pôster errado
  quando houver imagem correta**; backdrop horizontal; título, ano, gênero, sinopse, duração.
- **MOV-02** Categorias e coleções; pesquisa; favoritos; continuar assistindo; retomar de onde parou.
- **MOV-03** Tela de detalhes bonita e organizada. *(grade/detalhes OK; **pôsteres corretos bloqueados
  pelo TLS/RSA** — Etapa 3)*

### 4.5 Séries (SER)
- **SER-01** Catálogo exclusivo de séries; pôster correto; temporadas organizadas; episódios separados
  e ordenados; miniatura/imagem do episódio quando existir.
- **SER-02** Sinopse individual; progresso por episódio; próximo episódio; continuar assistindo.
  *(modelos e telas OK; arte do episódio depende do provedor/TMDB)*

### 4.6 Player (PLY)
- **PLY-01** Reproduz canais, filmes e episódios. *(OK)*
- **PLY-02** Seleção de áudio; seleção e **sincronização de legendas**; proporção de tela; retomar
  reprodução. *(áudio/legenda base OK; controles finos = Etapa 6)*
- **PLY-03** Controles intuitivos no DualShock 4; tratamento de falhas de conexão sem fechar o app.
  *(base OK; estabilidade = Etapas 6/9)*
- **PLY-04** Compatível com os formatos/codecs que o hardware e o FFmpeg do PS4 suportam. *(base OK)*

### 4.7 Motor interno e desempenho (ENG)
- **ENG-01** Listas M3U enormes (centenas de milhares de entradas) sem travar. *(CORRIGIDO8 elimina o
  reprocessamento por entrada; navegação com 292k entradas **a validar no PS4**)*
- **ENG-02** Cache persistente do catálogo por perfil; cache de imagens em disco; carregamento sob
  demanda; processamento em background; sem bloquear a navegação. *(OK)*
- **ENG-03** Paginação/índices eficientes; não carregar todas as imagens; sem vazamentos; meta 60 FPS
  quando viável. *(parcial — Etapa 7)*
- **ENG-04** Atualização de playlist sem perder o último catálogo válido. *(OK: falha de rede mantém
  a cópia salva e os dados locais)*

### 4.8 Segurança, imagens e conexão (SEC)
- **SEC-01** HTTPS com verificação de certificados **ligada**; corrigir a falha **RSA** do PS4 sem
  desativar a verificação. *(stack 2.28.10 pronta em `toolchain/tls-stack/ps4`; **nunca testada no
  console** — Etapa 3)*
- **SEC-02** Integração TMDB confiável: correspondência por título, ano e tipo; pôster/backdrop;
  imagens de episódio quando disponíveis. *(lógica existe; **bloqueada pelo TLS**)*
- **SEC-03** Recuperação de imagem quando uma fonte falha (TMDB → provedor → placeholder). *(base OK)*
- **SEC-04** EPG real (XMLTV) e atualização da programação. *(parser OK; teste real pendente)*
- **SEC-05** Reconexão e tratamento de servidores indisponíveis; timeouts; listas indisponíveis não
  derrubam o app. *(base OK; endurecer na Etapa 3/7)*

### 4.9 Configurações, idioma e identidade (CFG)
- **CFG-01** Português como padrão; 7 idiomas sincronizados; toda chave nova nas 7 línguas. *(OK)*
- **CFG-02** Configurações independentes por perfil (idioma, reprodução, interface). *(parcial —
  settings por perfil existem; escopo por perfil a revisar na Etapa 8)*
- **CFG-03** Gerenciamento dos usuários; diagnóstico e logs; tela de carregamento com identidade ZIM;
  ícone e identidade visual definitivos. *(parcial — Etapa 8)*
- **CFG-04** Mídia local (se mantida na versão final). *(existe; decidir na Etapa 8)*

---

## 5. Requisitos não-funcionais (NFR)

- **NFR-01** Estabilidade: sem CE-34878-0; sem travamentos com bibliotecas gigantes; testes prolongados.
- **NFR-02** Memória: controlada; texturas/imagens sob demanda; sem vazamentos.
- **NFR-03** Desempenho: 60 FPS quando viável, medido no PS4; animações não podem travar a navegação.
- **NFR-04** Logs/diagnósticos: linhas de log que permitam medir cache, parse, rede e TLS (já existem;
  usar nas evidências).
- **NFR-05** i18n: 7 línguas; nova chave ⇒ 7 arquivos.
- **NFR-06** Preservação: cada etapa mantém o comportamento das anteriores (regressão obrigatória).

## 6. Estágios

| Etapa | Objetivo | Estado | Critério de aceite (no PS4) |
|---|---|---|---|
| 1–2 | Perfis (10) + cache do catálogo | **CORRIGIDO8 entregue** | 10 perfis; 11º recusado; entrada de perfil carregado sem `parse …` no log; caches independentes; sobrevive a reinício |
| 3 | HTTPS/RSA, TMDB, imagens, EPG | **a começar (auditoria feita)** | TMDB carrega no PS4 com verificação ligada; pôsteres corretos por título/ano/tipo; EPG real associado aos canais |
| 4 | Catálogo completo (TV/filmes/séries) | a começar | Navegação completa, detalhes, temporadas, episódios, busca, favoritos, continue |
| 5 | Interface premium e animações | a começar | Crossfade do hero, zoom discreto do card, transições suaves; 60 FPS medido |
| 6 | Player completo | a começar | Áudio/legenda/sincronia/proporção/resume validados nos 3 tipos de mídia |
| 7 | Otimização, memória, listas gigantes | a começar | Estabilidade com 292k+ entradas; RAM controlada; sem vazamentos em sessão longa |
| 8 | Configurações, idiomas, acabamento | a começar | Escopo por perfil, ícone/identidade, loading, mídia local (decidir) |
| 9 | Regressão e estabilidade no PS4 | a começar | Suíte completa (§7) passada; logs limpos |
| 10 | PKG definitivo + código-fonte | a começar | Promoção **com autorização explícita do usuário**; oficial intacto |

### Etapa 3 — escopo detalhado (o próximo passo)
1. **TLS/RSA**: re-ligar o build ao stack mbedTLS 2.28.10 (`toolchain/tls-stack/ps4`) — a hipótese
   documentada é que o 2.16.6 falha na verificação de assinatura RSA (`0x4380`) e o 2.28 verifica
   `rsa_pss_rsae_sha256` — e **testar no console**. Nunca desligar verificação. Arquivos:
   `CMakeLists.txt` (variáveis `ZIMTV_TLS_STACK_DIR`/`ZIMTV_CURL_STACK_DIR`), `src/network/http.cpp`,
   `scripts/build-curl-tls3.sh`, `toolchain/tls-stack/**`.
2. **TMDB/pôsteres**: com o TLS resolvido, validar correspondência por título/ano/tipo
   (`src/images/tmdb_client.cpp`, `image_pipeline.cpp`), backoff por host, placeholder elegante.
3. **EPG**: testar com uma URL XMLTV real; associação por `tvg-id`/nome; atualização em background
   (`src/app/epg_service.cpp`, `src/epg/xmltv.h`).

## 7. Definição de pronto (DoD) e suíte de regressão no PS4

**DoD de cada etapa**: compilou + 28/28 + guard clean **E** os critérios de aceite da etapa foram
observados no console (log/medidas), sem regressão dos itens já entregues.

**Suíte de regressão (todas as etapas)**:
1. Primeiro início → seleção de usuários.
2. Criar Usuário 1 → fonte → 1ª carga.
3. Fechar/abrir → entrar no Usuário 1 sem reprocessar (log sem `parse`).
4. Criar Usuário 2 (outra fonte) → cargas independentes; trocar 1↔2.
5. Criar até 10; 11º recusado com mensagem; excluir libera vaga;reiniciar o PS4 e repetir.
6. Canal: X → player direto (sem tela de logo); zap; favoritos.
7. Filmes/Séries: navegar, detalhes, temporadas, episódios, continuar.
8. Pôsteres corretos (Etapa 3+); EPG real (Etapa 3+).
9. Circle nunca fecha o app; foco imediato; sem travamentos.
10. Atualizar lista manualmente sem apagar outros perfis; falha de rede preserva dados locais.

## 8. Riscos e bloqueadores atuais

- **RSA/TLS**: causa provável no stack antigo; corrigível pelo 2.28 (a validar). Bloqueador de TMDB.
- **EPG real**: nunca testado (a M3U do usuário não traz `url-tvg`); campo EPG por perfil existe.
- **Posters do provedor**: parte das logos vêm de host morto (`dlstvonline.com`) — fallback correto.
- **Validação**: tudo que não foi ao PS4 é "não comprovado" (regra RO-6).
- **Escopo**: animações Apple TV, paginação de catálogo gigante, proporção de tela, avatar escolhível e
  configurações por perfil ainda não implementados.

## 9. Histórico de builds (teste) e oficial

- CORRIGIDO6/7/8-TEST: Home V7 → fluxo de usuários primeiro → **10 perfis + cache do catálogo**.
- Entregas sempre com PKG + SOURCE.zip + CHANGELOG/README/CHECKSUMS + relatório em `Pkg novo/`
  (e `PKG PERTO/`).
- **`ZIMTV-IPTV-PS4.pkg` (oficial): nunca modificado.** Promoção só com autorização explícita.

---
*v1.0 — 2026-11-19 — mantido junto com o código (entra no SOURCE.zip).*
