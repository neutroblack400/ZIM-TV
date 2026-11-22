# ZIMTV — código da interface

Este pacote contém o projeto fonte e a versão web compilada. Não é um PKG instalável e não foi validado no PS4.

## O que usar
- `packages/web/src/web/pages/`: telas e estilos da interface.
- `packages/web/public/images/`: imagens demonstrativas.
- `packages/web/dist/`: saída web compilada (HTML, CSS, JavaScript e imagens).
- `design.md`: direção visual.

## Componentes
- `tv-shell.tsx`: modo TV, Gamepad API, atalhos e fullscreen.
- `zim-entry.tsx`: perfis, fontes demonstrativas e carregamento.
- `zim-dashboard.tsx`: início, filmes, séries, busca e detalhes.
- `zim-live.tsx`: TV ao vivo em três painéis.

## Integração no ZIMTV
Não basta renomear os arquivos para `.pkg`. A integração depende do código do ZIMTV, do runtime web usado pelo aplicativo, das permissões e do processo de empacotamento do seu projeto.

O HTML compilado usa caminhos absolutos `/assets/` e `/images/`. O host do aplicativo deve servir esses caminhos na raiz. Se seu runtime usa URLs locais ou outro prefixo, adapte o base do Vite e os caminhos de imagens antes de recompilar. Abrir diretamente por `file://` não foi testado e pode não funcionar.

A saída usa JavaScript, módulos e CSS modernos (incluindo color-mix e backdrop-filter). A versão do motor web no PS4 pode não oferecer suporte. Será necessário definir um alvo de compilação compatível e substituir recursos não suportados após testar no runtime real.

Gamepad API usa o mapeamento padrão do navegador; DualShock 4/PS4 não foi testado. Se o ZIMTV usa eventos nativos, conecte-os às ações `zim-tv-action`: `confirm`, `back`, `search`, `options`, `previous`, `next`. A ação confirmar no shell usa o elemento focado; eventos nativos de confirmação devem ativar o botão focado. Direções são tratadas via eventos `keydown` ArrowUp/Down/Left/Right.

## Limites da versão
- Catálogo e programação ilustrativos, sem streaming real.
- Formulários Xtream/M3U/EPG não conectam serviços.
- Perfis, URLs e favoritos ficam no localStorage do navegador.
- Usuário/senha de exemplo não são persistidos.
- Não há integração com player nativo, teclado nativo, APIs ou SDK do PS4.
- Imagens são referências demonstrativas; verifique licenças antes de distribuir.
- Mantida a instrumentação padrão do projeto gerenciado.

## Desenvolvimento
Instale Bun. Na raiz:
```
bun install
bun run dev
bun run build
```
O servidor usa a porta configurada em `__ports.cjs`. As dependências não estão incluídas neste ZIP.

Para uma adaptação específica ao PKG existente, é necessário fornecer o projeto fonte do ZIMTV e informar o runtime/renderizador empregado. Não envie credenciais reais.
