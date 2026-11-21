# ZIMTV IPTV — Plano de validação oficial (versão 14:51)

Regras:
- Nenhum item é marcado como aprovado sem teste no console PS4.
- Item falhou = parar e tratar antes de liberar a versão final.
- Em qualquer crash (CE-34878-0), coletar `/data/ZIMTV/logs/log.txt` (últimas 30–50 linhas) antes de desligar.

Ordem de validação:

1. **Inicialização e estabilidade** — abrir, chegar na tela "Escolha sua fonte IPTV", fechar, reabrir; sem crash.
2. **Teclado oficial centralizado** — OSK abre no centro; formulário permanece visível; texto digitado aparece no OSK.
3. **Colagem pelo PS App (prioridade máxima)** — colar texto curto e URL longa em campo focado; com OSK aberto e sem abrir OSK; sem CE-34878-0.
4. **Xtream Codes** — Conectar → valida, salva, abre TV ao vivo; categorias e canais carregam; conta/expiração visíveis.
5. **Importação M3U** — URL M3U → Carregar; indexa canais (log `saved playlist: N channels`); URL inválida → erro vermelho sem crash.
6. **Integração EPG** — adicionar guia XMLTV; status "Guia carregado: N programas"; "Programa atual" nos canais.
7. **Reprodução e persistência** — 2–3 canais ≥1 minuto cada; fechar e reabrir; fonte persiste e conecta de novo.
8. **Diagnóstico** — qualquer falha: anotar o passo exato e colar as últimas linhas de `log.txt`.
10. **Fallback DoH (sem depender do DNS do console)** — com o DNS do console quebrado (ex.: DNS manual `0.0.0.0`), o app ainda resolve pela via DoH: abrir Xtream Codes, lista M3U e EPG deve conectar e carregar catálogo/guia; imagens e downloads também. A reprodução ao vivo/vídeo usa o resolvedor do console (mpv/ffmpeg), portanto exige DNS manual no console (*Configurações → Rede → DNS → Manual: 1.1.1.1 / 1.0.0.1*).
9. **Tela de Configurações (v2, referência)** — abrir Configurações: fundo roxo radial, título da seção em branco grande no topo, barra lateral com ícones (pill roxa na categoria ativa), painel roxo "Resumo atual" na página Reprodução (Formato / Estabilidade / Próximo episódio), switches azuis grandes, dots de opção em forma de traço com o atual alongado e roxo, badge "Pronto"/"Salvo ✓" no topo direito da lista, banda escura no rodapé com o aviso do app à direita.

Ponto de falha conhecido no histórico: **CE-34878-0** ao confirmar/usar texto do OSK — corrigido (buffer de leitura limitado); confirmar no console antes de declarar a versão final.
