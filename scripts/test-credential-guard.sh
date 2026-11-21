#!/usr/bin/env bash
# Self-test of cmake/scan_credentials.cmake using synthetic fixtures only.
# Secret-like fixture values remain in a temporary directory and are never printed.
set -uo pipefail

repo="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cmake="$repo/toolchain/cmake/bin/cmake"
[[ -x "$cmake" ]] || cmake="$(command -v cmake || true)"
[[ -n "$cmake" ]] || { echo "CMake executable not found"; exit 1; }
script="$repo/cmake/scan_credentials.cmake"
tmp="$(mktemp -d "$repo/build/guard-test-XXXXXX")"
trap 'rm -rf "$tmp"' EXIT

# Deliberately synthetic Xtream-style fixture; never read a real/private config file.
source_fixture="$tmp/synthetic-secrets.txt"
printf 'TS=http://guard-test.example.invalid:8080/live/synthetic_user/synthetic_pass/123.ts\n' > "$source_fixture"
zero_secret_fixture="$tmp/zero-secrets.txt"
printf '# no secret values\nTS=<placeholder>\n' > "$zero_secret_fixture"
sources="$source_fixture|$tmp/also-synthetic.txt"
fail=0

guard() {
    local source_arg
    if (( $# >= 3 )); then source_arg="$3"; else source_arg="$sources"; fi
    "$cmake" "-DSCAN_DIR=$1" "-DSCAN_FILES=$2" "-DSECRET_SOURCES=$source_arg" -P "$script" >/dev/null 2>&1
    echo $?
}
expect() {   # name, should pass (1/0), exit code
    local ok=0
    if { [[ $2 == 1 ]] && [[ $3 == 0 ]]; } || { [[ $2 == 0 ]] && [[ $3 != 0 ]]; }; then ok=1; fi
    (( ok )) || fail=$((fail + 1))
    printf '%s  %s (exit %s, expected %s)\n' "$( ((ok)) && echo PASS || echo FAIL)" "$1" "$3" "$( [[ $2 == 1 ]] && echo success || echo failure)"
}

mkdir -p "$tmp/clean" "$tmp/empty" "$tmp/name" "$tmp/content" "$tmp/playlist" "$tmp/playlistcache"
printf 'nothing to see' > "$tmp/clean/readme.txt"
printf 'synthetic target' > "$tmp/content/clean.bin"
printf 'TS=<placeholder>' > "$tmp/name/test_streams.txt"
printf '#EXTM3U\n#EXTINF:-1,A\nhttp://stream.example.invalid/a.ts' > "$tmp/playlist/channels.M3U8"
printf '#EXTM3U' > "$tmp/playlistcache/m3u_playlist.json"
printf '\x00\x01\x02xsynthetic_pass\x00' > "$tmp/content/secret.bin"

expect 'valid synthetic source and clean input pass' 1 "$(guard "$tmp/clean" '')"
expect 'zero configured sources fail closed' 0 "$(guard "$tmp/clean" '' '')"
expect 'all configured source files missing fail closed' 0 "$(guard "$tmp/clean" '' "$tmp/missing-a.txt|$tmp/missing-b.txt")"
expect 'source with zero extractable secrets fails closed' 0 "$(guard "$tmp/clean" '' "$zero_secret_fixture")"
expect 'zero package and binary inputs fail closed' 0 "$(guard "$tmp/empty" '' "$source_fixture")"
expect 'file named test_streams.txt is rejected' 0 "$(guard "$tmp/name" '')"
expect 'an M3U / M3U8 playlist is rejected' 0 "$(guard "$tmp/playlist" '')"
expect 'a saved playlist copy is rejected' 0 "$(guard "$tmp/playlistcache" '')"
expect 'binary containing a synthetic password is rejected' 0 "$(guard "$tmp/clean" "$tmp/content/secret.bin")"

echo "credential guard self-test: $fail failure(s) (synthetic fixtures only)"
exit "$fail"
