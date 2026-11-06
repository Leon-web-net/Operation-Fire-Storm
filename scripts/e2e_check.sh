#!/usr/bin/env bash
# End-to-end check: run firestorm against CoreTech's test.py, then check the PDF.
# Usage: scripts/e2e_check.sh [challenge-dir] [firestorm-binary]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CHALLENGE="$(realpath "${1:-$ROOT/tests/coretech}")"
BIN="$(realpath "${2:-$ROOT/build/release/firestorm}")"
EXPECTED_SHA="44cb6bd48c6b51959f3ce43f945be7ca67ce38a4a5880190456b74dbe3f16974"

WORK="$(mktemp -d)"                  # run in a temp folder so the repo's 1.pdf is untouched
trap 'rm -rf "$WORK"' EXIT
cd "$WORK"

timeout 30 "$BIN" > firestorm.log 2>&1 &   # start firestorm in the background (killed after 30 s if it hangs)
FS_PID=$!
sleep 0.5                                  # give it time to start listening

(cd "$CHALLENGE" && python3 test.py --quiet) > test.log 2>&1 || true
wait "$FS_PID" || { echo "FAIL  firestorm exited with an error"; cat firestorm.log; exit 1; }

grep -q "^OK" test.log || { echo "FAIL  test.py"; cat test.log; exit 1; }
echo "PASS  test.py reports OK"

[ "$(sha256sum 1.pdf | cut -d' ' -f1)" = "$EXPECTED_SHA" ] || { echo "FAIL  1.pdf is wrong or missing"; exit 1; }
echo "PASS  1.pdf matches the expected SHA-256"