# Integrando a prévia ao ZIMTV original

Esta prévia é só navegável. Ela não conecta IPTV, EPG, login nem streaming, e não gera PKG. Para levar a interface ao ZIMTV real, siga estes passos.

## O que você precisa passar
1. Código-fonte do ZIMTV original (repositório ou pasta).
2. Runtime alvo: PS4 (versão de firmware), ou outro. A prévia roda em navegador.
3. Como o original carrega listas: Xtream, M3U, EPG (XMLTV), Stalker/MAG (se existir).
4. Como o original guarda perfis e favoritos hoje.

## Mapeamento de módulos
| Prévia | Papel | Onde plugar no original |
|---|---|---|
| `zim-store.ts` | Perfis, fonte e favoritos (`localStorage`) | Trocar por storage do app. Nunca guardar usuário/senha. |
| `zim-data.ts` | Catálogo e canais de demonstração | Substituir por dados vindos da fonte real (API Xtream, parser M3U, XMLTV). |
| `zim-entry.tsx` | Perfis, fonte, formulários, carregamento | Carregamento real: buscar catálogo e mostrar erros de rede/autenticação. |
| `zim-dashboard.tsx` | Início, filmes, séries, busca, configurações | Ligar a listas reais e ao player nativo. |
| `zim-live.tsx` | Ao vivo: categorias, canais, prévia | Usar grade EPG real e stream do canal. |
| `tv-shell.tsx` | Foco, teclado, gamepad, modo TV | Validar mapeamento do DualShock no PS4 antes de usar. |

## Pontos a validar no PS4
- Mapeamento do gamepad (padrão "standard" da Gamepad API) não foi testado com DualShock.
- Fullscreen e foco dependem do navegador do console ou do runtime escolhido.
- Desempenho de listas grandes e de imagens.

## Não fazer
- Não guardar usuário ou senha em `localStorage`.
- Não usar logos ou pôsteres reais sem licença.
