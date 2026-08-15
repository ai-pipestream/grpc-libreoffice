#!/usr/bin/env bash
# Generates Python gRPC stubs from the repo's proto tree into ./gen.
set -euo pipefail
cd "$(dirname "$0")"
PROTO_DIR=../../proto
mkdir -p gen
"${PYTHON:-.venv/bin/python}" -m grpc_tools.protoc \
  -I "$PROTO_DIR" \
  --python_out=gen \
  --grpc_python_out=gen \
  "$PROTO_DIR/ai/pipestream/document/v1/document.proto" \
  "$PROTO_DIR/ai/pipestream/office/v1/office_service.proto"
echo "stubs written to $(pwd)/gen"
