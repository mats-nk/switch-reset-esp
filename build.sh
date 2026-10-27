#!/usr/bin/env bash
# Builds the firmware for all supported boards and copies each binary to the project root,
# named after the hardware:
#
#   SwitchReset_ESP-01.bin      ESP-01 / ESP-01S        (env esp8266-esp01)
#   SwitchReset_Wemos-D1.bin    Wemos D1 / D1 mini      (env esp8266-d1mini)
#   SwitchReset_NodeMCU.bin     NodeMCU v2/v3 (ESP-12E) (env esp8266-nodemcu)
#   SwitchReset_Wemos-S2.bin    Wemos/LOLIN S2 mini     (env esp32-s2-mini)
#   SwitchReset_ESP32-C3.bin    ESP32-C3 (Super Mini..) (env esp32-c3)
#
# Usage:  ./build.sh                 build everything
#         ./build.sh c3 s2           build only boards whose hardware name contains one of the words
#         ./build.sh --list          list targets
# Needs PlatformIO (`pio`) or uv (https://docs.astral.sh/uv/) - with uv nothing else has to be installed.
set -uo pipefail
cd "$(dirname "$0")"

# env : hardware name used in the file name
TARGETS=(
  "esp8266-esp01:ESP-01"
  "esp8266-d1mini:Wemos-D1"
  "esp8266-nodemcu:NodeMCU"
  "esp32-s2-mini:Wemos-S2"
  "esp32-c3:ESP32-C3"
)

if [ "${1:-}" = "--list" ]; then
  for t in "${TARGETS[@]}"; do echo "${t#*:}  (env ${t%%:*})"; done
  exit 0
fi

if command -v pio >/dev/null 2>&1; then
  PIO=(pio)
elif command -v uv >/dev/null 2>&1; then
  PIO=(uv tool run platformio)
else
  echo "ERROR: neither 'pio' nor 'uv' found. Install uv: https://docs.astral.sh/uv/" >&2
  exit 1
fi

# Optional: run the host-side engine tests first (only needs g++)
if command -v g++ >/dev/null 2>&1; then
  echo "== Host tests =="
  ./tools/host_test/run.sh | tail -1 || { echo "ERROR: host tests failed" >&2; exit 1; }
fi

want() {  # does hardware name $1 match the command line filter?
  [ $# -eq 0 ] && return 0
  local name="${1,,}"; shift
  [ $# -eq 0 ] && return 0
  local w
  for w in "$@"; do [[ "$name" == *"${w,,}"* ]] && return 0; done
  return 1
}

ok=(); failed=()
for t in "${TARGETS[@]}"; do
  env="${t%%:*}"; hw="${t#*:}"
  want "$hw" "$@" || continue
  echo
  echo "== Building $hw ($env) =="
  if "${PIO[@]}" run -e "$env"; then
    src=".pio/build/$env/firmware.bin"
    dst="SwitchReset_${hw}.bin"
    if [ -f "$src" ]; then
      cp -f "$src" "$dst"
      ok+=("$dst ($(wc -c < "$dst") bytes)")
    else
      failed+=("$hw (firmware.bin not found)")
    fi
  else
    failed+=("$hw (build failed)")
  fi
done

echo
echo "== Result =="
for f in "${ok[@]:-}"; do [ -n "$f" ] && echo "  OK      $f"; done
for f in "${failed[@]:-}"; do [ -n "$f" ] && echo "  FAILED  $f"; done
[ ${#failed[@]} -eq 0 ] || exit 1
