# ZIMTV IPTV PS4 — relatório de correções

**Base corrigida:** `ZIMTV-IPTV-PS4-LIVETV2-TEST-SOURCE.zip`, fornecido nesta conversa. O código permanece C++17 nativo; não acrescentei chave real, playlist do usuário nem dado fictício de catálogo.

## O que foi corrigido

| Área | Alteração |
|---|---|
| Segurança do PKG | O gate de credenciais falha fechado se fontes privadas estiverem ausentes, vazias ou não forem varridas. O autoteste usa fixtures sintéticas. |
| Artes TMDB | A escolha exige título exato e ano compatível quando conhecido; a busca com `api_key` bloqueia downgrade HTTPS→HTTP. |
| Imagens | Limites para URLs pendentes, estados de falha do scheduler e admissão de itens maiores que o orçamento do LRU. |
| EPG | Cache só é reutilizado quando cobre os IDs requisitados; atualizações concorrentes são coalescidas e os horários respeitam relógio/fuso configurados. |
| Home e controle | Mantidos o tema violeta/índigo e quatro abas; L1/R1 navegam entre abas, o Hero vazio não deixa “Assistir” focável e as rails/header respeitam geração VOD e área TV-safe. |
| Playback e idioma | Fallback automático TS→HLS alinhado ao contrato. ratings e separadores numéricos respeitam a língua. PT-BR, EN, TR, ES, FR e DE/IT têm 985 chaves sincronizadas; textos restantes de playlist, teclado e categorias foram localizados. |
| Testes host | O runner compila as tabelas de tradução necessárias, executa `config_check` com URLs de exemplo redigidas e recompila quando headers pPlay mudam. Warnings que impediam `-Werror` foram corrigidos, sem desativar warnings. |

## Validação reproduzida

| Verificação | Resultado |
|---|---|
| Suíte estrita: `HOST_TEST_VARIANT=delivery-strict CXX=clang++-18 HOST_TEST_JOBS=4 bash scripts/test-host.sh` | 72.511 checks do app + 38 checks do parser/config; zero falhas, sem flags de supressão. |
| ASan/UBSan: `CXX=g++ bash scripts/test-host-sanitize.sh` | 72.511 + 38 checks; zero falhas e nenhum relatório de AddressSanitizer/UndefinedBehaviorSanitizer. |
| Gate: `bash scripts/test-credential-guard.sh` | 9 cenários sintéticos; zero falhas. |
| TMDB client | Compilação isolada com Clang 18, `-Wall -Wextra -Werror`, aprovada. |

`config_check` usou URLs `example.invalid` e credenciais inventadas; a saída redigiu usuário e senha.

## Limites que permanecem

O build oficial do PKG **não foi concluído**. `bash scripts/build-ps4iptv.sh Release` parou antes da configuração porque esta cópia não contém `toolchain/cmake/bin/cmake`; também faltam o Ninja local e prefixos de FFmpeg/TLS/curl e o sysroot/pacotes PacBrew esperados. Há alguns headers/ferramentas OpenOrbis no workspace, mas não o conjunto completo exigido pelo build. Nenhum `.pkg` foi gerado; ABI, renderização, instalação e uso em PS4 físico não foram validados.

Os testes host não substituem a verificação de DualShock 4, overscan de TV, streams/EPG reais ou desempenho 4K/HEVC. Não foram adicionadas opções de qualidade fictícias: isso depende das renditions realmente anunciadas pelo provedor/player.

A chave TMDB é opcional e vazia por padrão. Se for inserida manualmente em `settings.json`, ainda fica em texto claro; esta cópia não dispõe de armazenamento protegido da plataforma nem de UI própria para configurá-la. Nenhuma chave real foi incluída.
