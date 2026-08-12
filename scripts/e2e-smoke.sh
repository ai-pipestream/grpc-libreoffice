#!/usr/bin/env bash
# End-to-end smoke test: boots a private server instance on a free port,
# checks GetServiceInfo, streams one fixture through StreamPages and one
# through ConvertToPdf via the bench harness, and tears the server down.
# Exits nonzero on any failure. Requires a built tree (build/grlibre-server)
# and network for the first bench venv setup.
#
# Optional legs (default off; the base behavior is unchanged without flags):
#   --bench-unit  also run the bench unit tests (bench/test.sh, no server)
#   --clients     also run the cross-language client conformance test
#                 (scripts/test-clients.sh, which boots its own server)
set -euo pipefail
cd "$(dirname "$0")/.."

RUN_CLIENTS=0
RUN_BENCH_UNIT=0
for arg in "$@"; do
  case "$arg" in
    --clients) RUN_CLIENTS=1 ;;
    --bench-unit) RUN_BENCH_UNIT=1 ;;
    *) echo "unknown flag: $arg (known: --clients --bench-unit)"; exit 2 ;;
  esac
done

PORT="${SMOKE_PORT:-50153}"
FIXTURE="${SMOKE_FIXTURE:-fixtures/sample3.docx}"

[ -x build/grlibre-server ] || { echo "build/grlibre-server missing; build first"; exit 1; }
[ -f "$FIXTURE" ] || { echo "$FIXTURE missing; run fixtures/fetch.sh"; exit 1; }

GRLIBRE_TMPFS_DIR=/dev/shm GRLIBRE_PORT="$PORT" build/grlibre-server &
SERVER_PID=$!
trap 'kill "$SERVER_PID" 2>/dev/null || true; wait "$SERVER_PID" 2>/dev/null || true' EXIT

for _ in $(seq 1 50); do
  ss -ltn | grep -q ":$PORT " && break
  sleep 0.1
done
ss -ltn | grep -q ":$PORT " || { echo "server did not come up on :$PORT"; exit 1; }

bench/run.sh --target "localhost:$PORT" --files "$FIXTURE" \
  --iterations 1 --warmup 0 --modes pages-only,pdf --concurrency ""

# Tear the smoke server down before the optional legs; test-clients.sh boots
# its own instance on a different port.
kill "$SERVER_PID" 2>/dev/null || true
wait "$SERVER_PID" 2>/dev/null || true
trap - EXIT

if [ "$RUN_BENCH_UNIT" -eq 1 ]; then
  echo
  echo "== bench unit tests =="
  bench/test.sh
fi

if [ "$RUN_CLIENTS" -eq 1 ]; then
  echo
  echo "== client conformance =="
  scripts/test-clients.sh
fi

echo "SMOKE OK"
