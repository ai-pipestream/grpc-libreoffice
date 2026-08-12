#!/usr/bin/env bash
# One-command demo: boots the gRPC server and the web frontend if they are
# not already running, verifies the wiring, and prints the URL. Idempotent;
# `demo.sh --stop` tears both down.
set -euo pipefail
cd "$(dirname "$0")/.."

GRPC_PORT="${GRLIBRE_PORT:-50053}"
HTTP_PORT="${PORT:-8080}"

listening() { ss -ltn | grep -q ":$1 "; }

if [ "${1:-}" = "--stop" ]; then
  pkill -f "node .*frontend/server.mjs" 2>/dev/null && echo "stopped frontend" || echo "frontend not running"
  pkill -x grlibre-server 2>/dev/null && echo "stopped grlibre-server" || echo "grlibre-server not running"
  exit 0
fi

[ -x build/grlibre-server ] || {
  echo "build/grlibre-server missing; building..."
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
  cmake --build build -j"$(nproc)"
}

if listening "$GRPC_PORT"; then
  echo "gRPC server already on :$GRPC_PORT"
else
  GRLIBRE_TMPFS_DIR="${GRLIBRE_TMPFS_DIR:-/dev/shm}" GRLIBRE_PORT="$GRPC_PORT" \
    nohup build/grlibre-server > /tmp/grlibre-server.log 2>&1 &
  for _ in $(seq 1 50); do listening "$GRPC_PORT" && break; sleep 0.1; done
  listening "$GRPC_PORT" || { echo "server failed; see /tmp/grlibre-server.log"; exit 1; }
  echo "gRPC server started on :$GRPC_PORT (log: /tmp/grlibre-server.log)"
fi

if listening "$HTTP_PORT"; then
  echo "frontend already on :$HTTP_PORT"
else
  [ -d frontend/node_modules ] || (cd frontend && npm install --no-audit --no-fund)
  PORT="$HTTP_PORT" GRLIBRE_ADDR="localhost:$GRPC_PORT" \
    nohup node frontend/server.mjs > /tmp/grlibre-frontend.log 2>&1 &
  for _ in $(seq 1 50); do listening "$HTTP_PORT" && break; sleep 0.1; done
  listening "$HTTP_PORT" || { echo "frontend failed; see /tmp/grlibre-frontend.log"; exit 1; }
  echo "frontend started on :$HTTP_PORT (log: /tmp/grlibre-frontend.log)"
fi

curl -sf "localhost:$HTTP_PORT/api/info" > /dev/null \
  || { echo "frontend cannot reach the gRPC server"; exit 1; }

[ -f fixtures/sample3.docx ] || echo "note: fixtures/ is empty; run fixtures/fetch.sh for demo documents"
echo "demo ready: http://localhost:$HTTP_PORT"
