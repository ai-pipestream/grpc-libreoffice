#!/usr/bin/env bash
# Sets up a venv, generates stubs from ../proto, and runs bench.py.
# Everything lives under bench/; nothing else in the repo is touched.
set -euo pipefail
export BENCH_INVOKE_DIR="$PWD"
cd "$(dirname "$0")"

if [ ! -d .venv ]; then
  python3 -m venv .venv
  ./.venv/bin/pip install --quiet -r requirements.txt
fi

mkdir -p gen
./.venv/bin/python -m grpc_tools.protoc -I ../proto \
  --python_out=gen --grpc_python_out=gen \
  ../proto/ai/pipestream/document/v1/document.proto \
  ../proto/ai/pipestream/office/v1/office_service.proto
touch gen/__init__.py gen/ai/__init__.py gen/ai/pipestream/__init__.py \
  gen/ai/pipestream/office/__init__.py gen/ai/pipestream/office/v1/__init__.py \
  gen/ai/pipestream/document/__init__.py gen/ai/pipestream/document/v1/__init__.py

exec ./.venv/bin/python bench.py "$@"
