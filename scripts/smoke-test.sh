#!/usr/bin/env bash
# Boot-proofs a grpc-libreoffice image: a green build is not "done" until
# the artifact actually starts under the flags the compose stack runs it
# with. Hermetic (no documents, no network beyond the docker socket), so it
# runs in publish before any digest gets a tag. The image under test is the
# exact digest the leg pushed, pulled back from the registry.
#
#   1. closure: every shared library both shipped binaries link resolves
#      inside the image. The loader answers directly (LD_TRACE_LOADED_OBJECTS
#      is what ldd does), so no shell or ldd is needed in the image.
#   2. non-root: the image must not default to root (it ships USER grlibre).
#   3. boot: the server reaches its own "grpc-libreoffice listening on" line
#      under the hardened run flags the stack uses (read-only rootfs, tmpfs
#      at /tmp, no capabilities). The tmpfs mount matters: the server
#      refuses to start without one, so a green boot also proves uploaded
#      documents stay in RAM as designed.
set -euo pipefail

usage() {
  echo "Usage: $0 IMAGE" >&2
  exit 64
}
[[ $# -eq 1 ]] || usage
image=$1
container="grpc-libreoffice-smoke-$$"

cleanup() {
  docker rm -f "$container" >/dev/null 2>&1 || true
}

# Polls the container log for a line until it appears or the deadline passes.
wait_for_log() {
  local pattern=$1 deadline=$2
  for _ in $(seq 1 "$deadline"); do
    if docker logs "$container" 2>&1 | grep -q "$pattern"; then
      return 0
    fi
    if [[ "$(docker inspect -f '{{.State.Running}}' "$container" 2>/dev/null)" != "true" ]]; then
      break
    fi
    sleep 1
  done
  echo "container did not log '$pattern'; logs:" >&2
  docker logs "$container" >&2 || true
  return 1
}

echo "== smoke: library closure of the shipped binaries"
for binary in /opt/grlibre/grlibre-server /opt/grlibre/grlibre-worker; do
  trace=$(docker run --rm -e LD_TRACE_LOADED_OBJECTS=1 --entrypoint "$binary" "$image" 2>&1 || true)
  if ! grep -q '=>' <<<"$trace"; then
    echo "the loader printed no dependency list for $binary in $image:" >&2
    echo "$trace" >&2
    exit 1
  fi
  if grep -q "not found" <<<"$trace"; then
    echo "unresolved shared libraries for $binary in $image:" >&2
    grep "not found" <<<"$trace" >&2
    exit 1
  fi
done

echo "== smoke: image does not run as root"
image_user=$(docker inspect --format '{{.Config.User}}' "$image")
if [[ -z "$image_user" || "$image_user" == "root" || "$image_user" == "0" ]]; then
  echo "expected a non-root USER, image has '${image_user:-root}'" >&2
  exit 1
fi

echo "== smoke: boot to listening under the hardened run flags"
trap cleanup EXIT
docker run -d --name "$container" \
  --read-only --tmpfs /tmp:rw,size=512m \
  --cap-drop ALL --security-opt no-new-privileges:true \
  "$image" >/dev/null
boot_output=$(wait_for_log "grpc-libreoffice listening on" 90)
echo "$boot_output"
if grep -q "error while loading shared libraries" <<<"$boot_output"; then
  echo "the loader failed before main ran" >&2
  exit 1
fi
if grep -q "is not a tmpfs" <<<"$boot_output"; then
  echo "the server rejected the tmpfs mount" >&2
  exit 1
fi

echo "smoke-test: OK ($image)"
