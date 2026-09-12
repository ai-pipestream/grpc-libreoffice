#!/usr/bin/env bash
# Boot-proofs a grpc-libreoffice-ui image: the runtime stage carries no
# shell and no curl, so the gate is the image's own HEALTHCHECK, which asks
# node to fetch the index page at UI_BASE. Running the UI at a non-root
# UI_BASE also proves the base-path configurability the demo shell relies
# on. Hermetic and arch-portable (nothing but the docker socket), so every
# publish leg can run it on its own architecture. The image under test is
# the exact digest the leg pushed, pulled back from the registry.
set -euo pipefail

usage() {
  echo "Usage: $0 IMAGE" >&2
  exit 64
}
[[ $# -eq 1 ]] || usage
image=$1
container="grpc-libreoffice-ui-smoke-$$"

cleanup() {
  docker rm -f "$container" >/dev/null 2>&1 || true
}
trap cleanup EXIT

echo "== smoke: UI boots and its own healthcheck goes healthy"
docker run -d --name "$container" -e UI_BASE=/smoke "$image" >/dev/null

status=starting
for _ in $(seq 1 120); do
  status=$(docker inspect -f '{{if .State.Health}}{{.State.Health.Status}}{{else}}none{{end}}' "$container" 2>/dev/null || echo gone)
  case "$status" in
    healthy) break ;;
    unhealthy|none|gone|exited)
      echo "container health is '$status'; logs:" >&2
      docker logs "$container" >&2 || true
      exit 1
      ;;
  esac
  sleep 1
done
if [[ "$status" != "healthy" ]]; then
  echo "healthcheck did not go healthy in time (last '$status'); logs:" >&2
  docker logs "$container" >&2 || true
  exit 1
fi

echo "smoke-test-ui: OK ($image)"
