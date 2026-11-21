#!/usr/bin/env bash
# End-to-end check: run firestorm against CoreTech's test.py for a stage, then check the PDFs.
# Usage: scripts/e2e_check.sh [1|2] [firestorm-binary]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
STAGE="${1:-1}"
case "$STAGE" in 1|2) ;; *) echo "usage: $0 [1|2] [firestorm-binary]" >&2; exit 2 ;; esac
CHALLENGE="$ROOT/tests/coretech/stage_$STAGE"
BIN="$(realpath "${2:-$ROOT/build/release/firestorm}")"
EXPECTED_SHA="44cb6bd48c6b51959f3ce43f945be7ca67ce38a4a5880190456b74dbe3f16974"
ARGS=()
[ "$STAGE" = 2 ] && ARGS=(--stage2)

WORK="$(mktemp -d)"                  # run in a temp folder so the repo's PDFs are untouched
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

timeout 30 "$BIN" "${ARGS[@]}" > firestorm.log 2>&1 &   # start firestorm in the background (killed after 30 s if it hangs)
FS_PID=$!
# wait until it listens (up to ~5 s); don't connect to probe, it accepts only one connection
for _ in $(seq 50); do ss -ltn | grep -q ':5733 ' && break; sleep 0.1; done

(cd "$CHALLENGE" && python3 test.py --quiet) > test.log 2>&1 || true
wait "$FS_PID" || { echo "FAIL  firestorm exited with an error"; cat firestorm.log; exit 1; }

grep -q "^OK" test.log || { echo "FAIL  test.py"; cat test.log; exit 1; }
echo "PASS  test.py reports OK"

if [ "$STAGE" = 1 ]; then
    [ "$(sha256sum 1.pdf | cut -d' ' -f1)" = "$EXPECTED_SHA" ] || { echo "FAIL  1.pdf is wrong or missing"; exit 1; }
    echo "PASS  1.pdf matches the expected SHA-256"
else
    for i in 0 1 2 3 4; do
        cmp -s "$i.pdf" "$ROOT/unit_tests/data/stage2/$i.pdf" || { echo "FAIL  $i.pdf is wrong or missing"; exit 1; }
        echo "PASS  $i.pdf matches the expected file"
    done
fi
