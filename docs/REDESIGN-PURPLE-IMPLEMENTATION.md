# ZIM TV — redesign baseado nas referências fornecidas

## Alteração efetivamente implementada nesta revisão

- A tela `UsersScreen` foi reorganizada de círculos em uma fileira horizontal para cartões de perfil em duas colunas.
- Cada perfil agora aparece com bloco de monograma, nome legível e indicador de perfil ativo.
- O cartão de adicionar perfil permanece condicionado ao limite de 10 perfis.
- O botão de edição e as ações existentes de selecionar, renomear e excluir foram preservados.
- A grade foi dimensionada para acomodar até dez perfis em cinco linhas e duas colunas.

## Componentes existentes preservados

A base TLSDIAG15 já contém telas nativas para Home cinematográfica, TV ao vivo em três painéis,
catálogo de filmes, detalhes de séries/episódios, carregamento, erros, perfis, player e navegação.
Esta revisão não reescreve essas telas nem afirma que todas foram integralmente redesenhadas.

## Limitações / validação pendente

- O monograma é gerado automaticamente a partir do primeiro caractere do nome; ainda não há seletor persistente de avatar.
- Não foi possível validar visualmente no PS4 nesta revisão.
- Não foi gerado PKG: esta entrega é exclusivamente SOURCE.
- TLS e a implementação de rede não foram alterados.
