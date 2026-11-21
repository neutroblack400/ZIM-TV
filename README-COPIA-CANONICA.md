# ZIMTV-IPTV-PS4 — SOURCE CANÔNICO (TLSTEST)

**Esta é a fonte do último PKG criado e testado:**
`/mnt/root/zimtv-arquivos/ZIMTV-IPTV-PS4-TLSTEST.pkg`
md5: `7569e3ea8d07413e1a7e593347d9e199`

Conferido nesta cópia: `build-ps4/IV0001-ZIMT00001_00-ZIMT000010002000.pkg` tem exatamente o mesmo md5.
A árvore foi copiada de `/tmp/opencode/zimtv-build` (a fonte original usada durante o desenvolvimento) para
este sdcard exFAT, que é persistente — antes disso, a única cópia existia em tmpfs (RAM) e se perderia num reboot.

**Regra permanente: o source do ZIMTV vive aqui, nunca só em `/tmp`.** Após qualquer alteração local, sincronizar para cá.

## O que esta árvore contém (prova)

- `src/iptv/classify.cpp` — classificação M3U (live/movies/series)
- `toolchain/tls-stack/ps4/` — mbedTLS 2.28.10 PS4 (`prefix/`), libcurl 7.80.0 linkada nela (`prefix-curl/`)
- `toolchain/ffmpeg-5.0-tls/` — FFmpeg 5.0 (build antigo, mbedTLS 2.16) — o app ainda usa este para playback
- `scripts/build-curl-tls3.sh`, `scripts/build-ffmpeg-tls-3.sh` — rebuilds de TLS
- `CMakeLists.txt` com `ZIMTV_TLS_STACK_DIR` / `ZIMTV_CURL_STACK_DIR` / `ZIMTV_FFMPEG_TLS_DIR`
- `build-ps4/` — build configurado (CMakeCache aponta para o caminho antigo `/tmp/opencode/zimtv-build`; para rebuild, apagar e reconfigurar)

## Diferenças entre esta cópia e o source original (todas deliberadas, exFAT não suporta symlinks)

| Item | Original (symlink p/ …) | Aqui |
|---|---|---|
| `toolchain/ninja/ninja` | `/data/data/com.termux/files/usr/bin/ninja` | wrapper `exec` idem |
| `toolchain/cmake/bin/cmake` | idem | wrapper `exec` idem |
| `toolchain/llvm-12.0.1/` | prefix inteiro do Termux (~518k arquivos) | diretório real c/ wrappers para clang, clang++, ld.lld, llvm-ar, llvm-nm, llvm-ranlib, llvm-strip, llvm-objcopy, llvm-objdump |
| `toolchain/pacbrew/.../openorbis` | `/root/zimtv-arquivos/opt/pacbrew/ps4/openorbis` | diretório-placeholder com `LEIA-ME.txt` |
| `toolchain/ps4-shadow-sysroot/.../openorbis` | idem | diretório-placeholder com `LEIA-ME.txt` |
| `toolchain/src/curl-tls3/curl-7.80.0/lib/.libs/libcurl.la` | `../libcurl.la` | cópia real do `libcurl.la` |
| `toolchain/tls-stack/ps4/ps4-clang` | caminhos absolutos `/tmp/...` | autolocalizável (HERE) + sysroot persistente (override: `OPENORBIS=...`) |
| `scripts/build-curl-tls3.sh` | `SYSROOT=/tmp/.../ps4-shadow-sysroot/...` | `SYSROOT` = sysroot persistente + `SYSROOT_DIR` derivado (pai de `opt/...`) p/ `PKG_CONFIG_SYSROOT_DIR` |
| `scripts/build-ffmpeg-tls-3.sh` | `OPENORBIS=/tmp/...` (mesmo padrão) | idem |

Sysroot persistente (read-only, usado pelos builds):
`/mnt/root/zimtv-arquivos/opt/pacbrew/ps4/openorbis`

## Rebuild a partir daqui

1. `rm -rf build-ps4 && mkdir build-ps4 && cd build-ps4`
2. Reconfigurar com os mesmos flags do build anterior (ver `CMakeCache.txt` antigo como referência;
   ou script `scripts/build-ps4iptv.sh`, que reconfigura sozinho — mas leia o script antes)
3. Validar o pacote com PkgTool `pkg_validate` antes de entregar (`PkgTool.Core` em `UCDownloads/PkgTool.Core`)

Os diretórios `toolchain/src/*/` e os `.pc`/`.la`/`curl-config`/`ffbuild` com caminhos `/tmp/...` são artefatos
de builds anteriores — inertes (o link do app usa `ZIMTV_*_STACK_DIR` diretamente, não pkg-config) e recriados
apelo rebuild. O FFmpeg em uso no TLSTEST é o `toolchain/ffmpeg-5.0-tls/` (2.16); o rebuild do FFmpeg contra
2.28 ainda não foi concluído (`scripts/build-ffmpeg-tls-3.sh` existe pronto para isso).

## Regra de ouro

Não promover nenhum PKG a oficial sem teste no console + autorização explícita do usuário ("sim").
TLSTEST permanece em teste; o PKG oficial `ZIMTV-IPTV-PS4.pkg` não é tocado por este trabalho.

## Atualizações de 2026-11-16 (PASSO 1 + TLSDIAG)

- `cmake/ps4-linux.cmake`: default de `PS4_PACBREW_SYSROOT` passou a ser `/root/zimtv-arquivos`
  (era `${PS4_TOOLCHAIN_ROOT}/pacbrew`, um symlink que não existe neste checkout exFAT).
  Motivo: o `pkg-config` deste projeto reexecuta a toolchain para detecção de compilador e usava o
  default; sem o sysroot persistente o FreeType apontava para um caminho inexistente e o build não compilava.
  Nenhuma outra mudança funcional; o `-DPS4_PACBREW_SYSROOT` na linha de comando não tinha efeito para esse uso.
- Instrumentação de diagnóstico TLS (PASSO 2, solicitado e autorizado): a instrumentação está em
  `src/network/http.h` (`curlCode` em Response), `src/network/http.cpp` (grava o código numérico do CURL),
  `src/images/image_loader.cpp` (detalhe em log passa a incluir `[curl=N, host=…]`) e
  `src/images/image_pipeline.cpp` (caminho permanente de imagem não-200 registra `r.detail` completo;
  antes só virava "HTTP 0", sem o motivo do certificado).
  Nenhuma alteração em parser, catálogo, player, interface, EPG ou verificação TLS.
- Build canônico validado do zero: PKG de prova `build-ps4/IV0001-ZIMT00001_00-ZIMT000010002000.pkg`
  (md5 8cf2918a99e25cd865838721b4d07008) com pkg_validate 28/28 [OK]. Build por script requer
  host-bin + build dir fora do sdcard (FUSE não executa binários de lá): ver seção "Rebuild" acima;
  fonte: /root/zimtv-arquivos/Pkg novo/ZIMTV-IPTV-PS4 (bind sem espaço, usado para compilar).

## Atualizações de 2026-11-16 (renomeação + TLSDIAG3)

- A pasta UCDownloads foi renomeada para **ZIM TV ARQUIVOS**. O caminho canônico agora é
  `/mnt/sdcard/ZIM TV ARQUIVOS/Pkg novo/ZIMTV-IPTV-PS4`. O symlink sem espaço
  `/root/zimtv-arquivos -> /mnt/sdcard/ZIM TV ARQUIVOS` foi criado para uso do pkg-config.
- `cmake/ps4-linux.cmake`: `PS4_PACBREW_SYSROOT` default = `/root/zimtv-arquivos`
  (aponta para a raiz que contém opt/pacbrew/ps4/openorbis).
- `scripts/build-curl-tls3.sh`, `scripts/build-ffmpeg-tls-3.sh`, `toolchain/tls-stack/ps4/ps4-clang`:
  sysroot aponta para `/root/zimtv-arquivos/opt/pacbrew/ps4/openorbis`.
- libcurl 7.80.0 reconstruída com instrumentação TLS-DIAG2: dumps da cadeia e do verify
  result vão APENAS para `/data/ZIMTV/tls-diag.log` (a escrita em `stderr` foi removida
  porque é a suspeita principal do CE-34878-0 observado no console com o TLSDIAG2).
- `build-ps4` e `build/` são descartáveis: build canônico por symlink
  `/tmp/opencode/canonical-src -> árvore canônica` e diretório de build em `/tmp` (exFAT do sdcard
  não executa binários; pkg validation exige PkgTool aarch64 fora dele).
- Espelho íntegro em `/root/zimtv-source-mirror-2026-11-16` (f2fs), mantido em sincronia
  com a árvore canônica.
