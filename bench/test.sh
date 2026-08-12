#!/usr/bin/env bash
# Runs the bench unit tests (test_bench.py) inside the bench venv. Creates
# the venv and generates stubs into gen/ exactly like run.sh when missing;
# no server is needed. Extra args go to pytest.
set -euo pipefail
cd "$(dirname "$0")"

if [ ! -d .venv ]; then
  python3 -m venv .venv
  ./.venv/bin/pip install --quiet -r requirements.txt
fi
# a pre-existing venv may predate pytest's addition to requirements.txt
./.venv/bin/python -c 'import pytest' 2>/dev/null \
  || ./.venv/bin/pip install --quiet -r requirements.txt

if [ ! -f gen/ai/pipestream/office/v1/office_service_pb2.py ]; then
  mkdir -p gen
  ./.venv/bin/python -m grpc_tools.protoc -I ../proto \
    --python_out=gen --grpc_python_out=gen \
    ../proto/ai/pipestream/office/v1/office_service.proto
  touch gen/__init__.py gen/ai/__init__.py gen/ai/pipestream/__init__.py \
    gen/ai/pipestream/office/__init__.py gen/ai/pipestream/office/v1/__init__.py
fi

exec ./.venv/bin/python -m pytest test_bench.py "$@"
