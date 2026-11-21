# ZIMTV · Design (prévia web)

## Identidade
- Marca: "ZIM" em lime `#c1f873` e "TV" em rosa `#f577d2`. Logo em `public/images/zimtv-logo.png`.
- Slogan: "Sua TV, do seu jeito."
- Visual cinematográfico: fundo `#100921`, violeta `#7942ec`, texto lilás, cantos arredondados, bordas lilás finas.
- Sem visual de painel administrativo. Sem logos, pôsteres ou artes reais: só gradientes e imagens geradas.

## Tokens (`packages/web/src/web/zim/zim.css`)
| Token | Valor |
|---|---|
| `--zim-bg` | `#100921` |
| `--zim-violet` | `#7942ec` |
| `--zim-lime` | `#c1f873` |
| `--zim-pink` | `#f577d2` |
| `--zim-lilac` | lilás do texto secundário |
| `--zim-line` | borda lilás fina |
| `--zim-muted` | texto de apoio |

## Escala e TV
- Modo TV ligado por padrão. Escala base `--zim-ui-scale: 1.15` (16px × 1.15), 19px em telas ≥1600px.
- Margem de segurança de ~5%.
- Rodapé com símbolos de controle (✥ ✕ ○ △ □ L1/R1).
- Responsivo abaixo de 900px, sem rolagem horizontal.

## Foco
- Elementos navegáveis recebem `.zf`. O foco inicial vem de `data-autofocus="true"`.
- Modais e player marcam `data-modal-open`; o foco fica restrito ao modal mais alto.
- Anel luminoso no card focado.

## Movimento
- Animações curtas (rise de modal, progresso de carregamento).
- `prefers-reduced-motion` e o interruptor em Configurações desligam animações.
