# ZIMTV
- Plano aprovado: interface web apenas, baseada no ZIP e Apple TV.
- Implementado: início, canais demo, catálogo, busca, favoritos locais, detalhes/episódios, player demo, configurações, foco espacial por teclado.
- Build concluído.
- Pendente: teste em navegador; compatibilidade PS4 não verificada e sem integração IPTV.

## Revisão roxa / perfis
- Destino confirmado pelo usuário: ZIMTV PKG para PS4. Entrega atual é prévia web, não pacote instalável; integração exige projeto e validação no console.
- Implementados perfis locais (criar/editar/selecionar), mural de pôsteres, identidade verde/rosa sobre roxo, fontes Xtream/M3U e EPG opcional, carregamento demo e dashboard roxo.
- Nome/URL de fonte locais; usuário e senha descartados, nenhuma conexão externa.
- Build OK. Playwright Chrome: criação/persistência, fluxo Xtream, carregamento, ausência de senha no storage, favoritos isolados e largura mobile OK, sem erros JS.
- Não testado: PS4/gamepad nativo, integração PKG, reprodução.
