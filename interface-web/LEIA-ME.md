# Interface ZIMTV — prévia TV/PS4

Abra `ZIMTV-COMPLETO.html` para a versão com JavaScript, estilos e imagens incorporados em um arquivo. Testada por file:// no Chrome, não no PS4. Fontes web e instrumentação podem fazer requisições externas.

O projeto fonte completo está em `packages/`; saída compilada em `packages/web/dist/`. Veja `INTEGRACAO-PS4.md`. Esta pasta não altera o aplicativo C++ e não gera nem integra automaticamente um PKG. Conteúdo, player e EPG são demonstrativos.

O exportador `exportar-html.py` usa caminhos do sandbox original; ajuste as variáveis `root` e o caminho de saída para sua máquina antes de executá-lo.
