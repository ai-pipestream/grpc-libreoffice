#!/usr/bin/env bash
# Cross-language client conformance test: boots a private server instance on
# a free port (default 50253, override with CLIENTS_TEST_PORT), then drives
# the Python, Node, and Java example clients against it:
#
#   info    succeeds in each language
#   pages   renders fixtures/sample3.docx; every language must produce the
#           same number of page-*.png files, each non-empty with PNG magic
#   pdf     converts the same fixture; output starts with %PDF and the byte
#           sizes across languages agree within 1%
#   error   a file with an unresolvable extension must fail (nonzero exit)
#   dpi     pages --dpi 72 against the server's 144-dpi default: the first
#           PNG's pixel width must be exactly half the default run's width
#   range   pages --first-page 2 --last-page 2 --parts PAGES: exactly one
#           page-0002.png (document-absolute index)
#   format  pages --format webp --parts PAGES: page-*.webp with RIFF/WEBP magic
#   gray    pages --grayscale --parts PAGES: still PNG magic
#   width   pages --max-width 200 --parts PAGES: first page width <= 200
#   svg     pages --format svg --parts PAGES: page-*.svg containing <svg
#
# Prints a per-language PASS/FAIL table and exits nonzero on any failure.
# Client setup (venv, stubs, node_modules, gradle installDist) happens lazily.
# Requires a built tree (build/grlibre-server).
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"

PORT="${CLIENTS_TEST_PORT:-50253}"
FIXTURE="${CLIENTS_TEST_FIXTURE:-fixtures/sample3.docx}"

[ -x build/grlibre-server ] || { echo "build/grlibre-server missing; build first"; exit 1; }
[ -f "$FIXTURE" ] || { echo "$FIXTURE missing; run fixtures/fetch.sh"; exit 1; }
FIXTURE="$ROOT/$FIXTURE"

WORK="$(mktemp -d /tmp/grlibre-test-clients.XXXXXX)"
mkdir -p "$WORK/logs"
SERVER_PID=""
cleanup() {
  [ -n "$SERVER_PID" ] && { kill "$SERVER_PID" 2>/dev/null || true; wait "$SERVER_PID" 2>/dev/null || true; }
  rm -rf "$WORK"
}
trap cleanup EXIT

# ---- lazy per-language setup -------------------------------------------------

setup_python() {
  local dir="$ROOT/clients/python"
  if [ ! -x "$dir/.venv/bin/python" ]; then
    echo "[setup] creating clients/python/.venv"
    python3 -m venv "$dir/.venv"
    "$dir/.venv/bin/pip" install --quiet -r "$dir/requirements.txt"
  fi
  local stub="$dir/gen/ai/pipestream/office/v1/office_service_pb2.py"
  local proto="$ROOT/proto/ai/pipestream/office/v1/office_service.proto"
  if [ ! -f "$stub" ] || [ "$proto" -nt "$stub" ]; then
    echo "[setup] generating python stubs"
    (cd "$dir" && PYTHON=.venv/bin/python ./generate.sh)
  fi
}

setup_node() {
  local dir="$ROOT/clients/node"
  if [ ! -d "$dir/node_modules" ]; then
    echo "[setup] npm ci in clients/node"
    (cd "$dir" && npm ci)
  fi
}

JAVA_LAUNCHER="$ROOT/clients/java/build/install/grlibre-java-client/bin/grlibre-java-client"
setup_java() {
  local src="$ROOT/clients/java/src/main/java/ai/pipestream/office/examples/OfficeClient.java"
  local proto="$ROOT/proto/ai/pipestream/office/v1/office_service.proto"
  if [ ! -x "$JAVA_LAUNCHER" ] || [ "$src" -nt "$JAVA_LAUNCHER" ] \
      || [ "$proto" -nt "$JAVA_LAUNCHER" ]; then
    echo "[setup] gradle installDist in clients/java"
    (cd "$ROOT/clients/java" && ./gradlew -q installDist)
  fi
}

setup_python
setup_node
setup_java

# ---- client runners (uniform: run_client <lang> <args...>) -------------------

run_client() {
  local lang="$1"; shift
  case "$lang" in
    python) "$ROOT/clients/python/.venv/bin/python" "$ROOT/clients/python/client.py" "$@" ;;
    node)   node "$ROOT/clients/node/client.js" "$@" ;;
    java)   "$JAVA_LAUNCHER" "$@" ;;
  esac
}

# ---- private server -----------------------------------------------------------

GRLIBRE_TMPFS_DIR=/dev/shm GRLIBRE_PORT="$PORT" build/grlibre-server &
SERVER_PID=$!
for _ in $(seq 1 50); do
  ss -ltn | grep -q ":$PORT " && break
  sleep 0.1
done
ss -ltn | grep -q ":$PORT " || { echo "server did not come up on :$PORT"; exit 1; }
export GRLIBRE_ADDR="localhost:$PORT"

# ---- checks -------------------------------------------------------------------

LANGS=(python node java)
TESTS=(info pages pdf error dpi range format gray width svg)
declare -A RESULT   # RESULT[lang/test] = PASS | FAIL(reason)
declare -A PAGE_COUNT PDF_SIZE PAGE_WIDTH
FAILED=0

fail() { # fail <lang> <test> <reason>
  RESULT["$1/$2"]="FAIL($3)"
  FAILED=1
  echo "  FAIL  $1 $2: $3  (log: $WORK/logs/$1-$2.log)"
  tail -n 5 "$WORK/logs/$1-$2.log" 2>/dev/null | sed 's/^/        /' || true
}
pass() { RESULT["$1/$2"]="PASS"; }

is_png() { [ -s "$1" ] && [ "$(head -c 8 "$1" | od -An -tx1 | tr -d ' \n')" = "89504e470d0a1a0a" ]; }

# WebP: RIFF....WEBP
is_webp() {
  [ -s "$1" ] && [ "$(head -c 4 "$1")" = "RIFF" ] \
    && [ "$(dd if="$1" bs=1 skip=8 count=4 2>/dev/null)" = "WEBP" ]
}

is_svg() { [ -s "$1" ] && grep -q "<svg" "$1"; }

# IHDR pixel width: bytes 16..19 of the file, big-endian.
png_width() {
  python3 -c 'import struct, sys
with open(sys.argv[1], "rb") as f:
    f.seek(16)
    print(struct.unpack(">I", f.read(4))[0])' "$1"
}

BAD_FILE="$WORK/x.unknownext"
cp "$FIXTURE" "$BAD_FILE"

for lang in "${LANGS[@]}"; do
  echo "== $lang =="

  # info
  if run_client "$lang" info >"$WORK/logs/$lang-info.log" 2>&1; then
    pass "$lang" info
  else
    fail "$lang" info "exit $?"
  fi

  # pages
  outdir="$WORK/pages-$lang"
  mkdir -p "$outdir"
  if run_client "$lang" pages "$FIXTURE" "$outdir" >"$WORK/logs/$lang-pages.log" 2>&1; then
    count=0; bad=""
    for f in "$outdir"/page-*.png; do
      [ -e "$f" ] || continue
      count=$((count + 1))
      is_png "$f" || bad="$bad $(basename "$f")"
    done
    PAGE_COUNT[$lang]=$count
    if [ "$count" -eq 0 ]; then
      fail "$lang" pages "no page-*.png produced"
    elif [ -n "$bad" ]; then
      fail "$lang" pages "bad PNG:$bad"
    else
      pass "$lang" pages
      PAGE_WIDTH[$lang]=$(png_width "$outdir/page-0001.png")
    fi
  else
    fail "$lang" pages "exit $?"
  fi

  # dpi: --dpi 72 against the 144-dpi server default must exactly halve the
  # first page's pixel width relative to the default pages run above
  dpidir="$WORK/pages-$lang-dpi72"
  mkdir -p "$dpidir"
  if run_client "$lang" pages "$FIXTURE" "$dpidir" --dpi 72 >"$WORK/logs/$lang-dpi.log" 2>&1; then
    base="${PAGE_WIDTH[$lang]:-}"
    if [ -z "$base" ]; then
      fail "$lang" dpi "no baseline width (default pages run failed)"
    elif ! is_png "$dpidir/page-0001.png"; then
      fail "$lang" dpi "no valid page-0001.png produced"
    else
      w=$(png_width "$dpidir/page-0001.png")
      if [ $((w * 2)) -eq "$base" ]; then
        pass "$lang" dpi
      else
        fail "$lang" dpi "width $w at 72 dpi, expected half of $base"
      fi
    fi
  else
    fail "$lang" dpi "exit $?"
  fi

  # range: page 2 only, document-absolute filename, pages-only so it stays cheap
  rangedir="$WORK/pages-$lang-range"
  mkdir -p "$rangedir"
  if run_client "$lang" pages "$FIXTURE" "$rangedir" \
      --first-page 2 --last-page 2 --parts PAGES \
      >"$WORK/logs/$lang-range.log" 2>&1; then
    count=0
    for f in "$rangedir"/page-*; do
      [ -e "$f" ] || continue
      count=$((count + 1))
    done
    if [ "$count" -ne 1 ]; then
      fail "$lang" range "expected 1 page file, got $count"
    elif ! is_png "$rangedir/page-0002.png"; then
      fail "$lang" range "missing page-0002.png (document-absolute index)"
    else
      pass "$lang" range
    fi
  else
    fail "$lang" range "exit $?"
  fi

  # format: WebP magic and .webp extension
  fmtdir="$WORK/pages-$lang-webp"
  mkdir -p "$fmtdir"
  if run_client "$lang" pages "$FIXTURE" "$fmtdir" \
      --format webp --parts PAGES \
      >"$WORK/logs/$lang-format.log" 2>&1; then
    count=0; bad=""
    for f in "$fmtdir"/page-*.webp; do
      [ -e "$f" ] || continue
      count=$((count + 1))
      is_webp "$f" || bad="$bad $(basename "$f")"
    done
    if [ "$count" -eq 0 ]; then
      fail "$lang" format "no page-*.webp produced"
    elif [ -n "$bad" ]; then
      fail "$lang" format "bad WebP:$bad"
    else
      pass "$lang" format
    fi
  else
    fail "$lang" format "exit $?"
  fi

  # grayscale: still a PNG
  graydir="$WORK/pages-$lang-gray"
  mkdir -p "$graydir"
  if run_client "$lang" pages "$FIXTURE" "$graydir" \
      --grayscale --parts PAGES \
      >"$WORK/logs/$lang-gray.log" 2>&1; then
    if ! is_png "$graydir/page-0001.png"; then
      fail "$lang" gray "no valid page-0001.png produced"
    else
      pass "$lang" gray
    fi
  else
    fail "$lang" gray "exit $?"
  fi

  # max-width: first page at most 200 px wide
  widthdir="$WORK/pages-$lang-width"
  mkdir -p "$widthdir"
  if run_client "$lang" pages "$FIXTURE" "$widthdir" \
      --max-width 200 --parts PAGES \
      >"$WORK/logs/$lang-width.log" 2>&1; then
    if ! is_png "$widthdir/page-0001.png"; then
      fail "$lang" width "no valid page-0001.png produced"
    else
      w=$(png_width "$widthdir/page-0001.png")
      if [ "$w" -gt 0 ] && [ "$w" -le 200 ]; then
        pass "$lang" width
      else
        fail "$lang" width "width $w, expected <= 200"
      fi
    fi
  else
    fail "$lang" width "exit $?"
  fi

  # svg: vector page files carry an <svg tag
  svgdir="$WORK/pages-$lang-svg"
  mkdir -p "$svgdir"
  if run_client "$lang" pages "$FIXTURE" "$svgdir" \
      --format svg --parts PAGES \
      >"$WORK/logs/$lang-svg.log" 2>&1; then
    count=0; bad=""
    for f in "$svgdir"/page-*.svg; do
      [ -e "$f" ] || continue
      count=$((count + 1))
      is_svg "$f" || bad="$bad $(basename "$f")"
    done
    if [ "$count" -eq 0 ]; then
      fail "$lang" svg "no page-*.svg produced"
    elif [ -n "$bad" ]; then
      fail "$lang" svg "missing <svg tag:$bad"
    else
      pass "$lang" svg
    fi
  else
    fail "$lang" svg "exit $?"
  fi

  # pdf
  pdf="$WORK/out-$lang.pdf"
  if run_client "$lang" pdf "$FIXTURE" "$pdf" >"$WORK/logs/$lang-pdf.log" 2>&1; then
    if [ -s "$pdf" ] && [ "$(head -c 4 "$pdf")" = "%PDF" ]; then
      PDF_SIZE[$lang]=$(stat -c %s "$pdf")
      pass "$lang" pdf
    else
      fail "$lang" pdf "missing %PDF magic"
    fi
  else
    fail "$lang" pdf "exit $?"
  fi

  # error path: unresolvable extension must fail
  if run_client "$lang" pdf "$BAD_FILE" "$WORK/should-not-exist-$lang.pdf" \
      >"$WORK/logs/$lang-error.log" 2>&1; then
    fail "$lang" error "unresolvable extension accepted (exit 0)"
  else
    pass "$lang" error
  fi
done

# ---- cross-language agreement --------------------------------------------------

echo "== cross-language =="

counts=$(printf '%s\n' "${PAGE_COUNT[@]-}" | sort -u | sed '/^$/d')
if [ "$(echo "$counts" | wc -l)" -eq 1 ] && [ ${#PAGE_COUNT[@]} -eq ${#LANGS[@]} ]; then
  echo "  page count agrees across languages: $counts"
else
  echo "  FAIL  page counts differ: $(for l in "${LANGS[@]}"; do printf '%s=%s ' "$l" "${PAGE_COUNT[$l]:-?}"; done)"
  FAILED=1
fi

if [ ${#PDF_SIZE[@]} -eq ${#LANGS[@]} ]; then
  min=$(printf '%s\n' "${PDF_SIZE[@]}" | sort -n | head -1)
  max=$(printf '%s\n' "${PDF_SIZE[@]}" | sort -n | tail -1)
  if [ $((max * 100)) -le $((min * 101)) ]; then
    echo "  pdf sizes within 1%: $(for l in "${LANGS[@]}"; do printf '%s=%s ' "$l" "${PDF_SIZE[$l]}"; done)"
  else
    echo "  FAIL  pdf sizes diverge >1%: $(for l in "${LANGS[@]}"; do printf '%s=%s ' "$l" "${PDF_SIZE[$l]}"; done)"
    FAILED=1
  fi
else
  echo "  (pdf size comparison skipped: not every language produced a pdf)"
fi

# ---- summary --------------------------------------------------------------------

echo
printf '%-8s' "lang"
for t in "${TESTS[@]}"; do printf '%-8s' "$t"; done
echo
for lang in "${LANGS[@]}"; do
  printf '%-8s' "$lang"
  for t in "${TESTS[@]}"; do
    r="${RESULT[$lang/$t]:-SKIP}"
    printf '%-8s' "${r%%(*}"
  done
  echo
done

if [ "$FAILED" -ne 0 ]; then
  echo
  echo "CLIENTS TEST FAILED"
  exit 1
fi
echo
echo "CLIENTS TEST OK"
