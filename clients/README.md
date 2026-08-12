# Example clients

Self-contained example CLI clients for the `OfficeRenderService` gRPC API
(`proto/ai/pipestream/office/v1/office_service.proto`), in Python, Node.js,
and Java. Each client exposes the same three subcommands:

| Subcommand | What it does |
|---|---|
| `info` | Print server/LibreOffice versions, limits, and accepted formats. |
| `pages <file> [outdir]` | Upload the file, save every rendered page as `page-NNNN.png`, summarize typed content events, print RenderStatus and timing. |
| `pdf <file> [out.pdf]` | Upload the file, write the streamed PDF, print byte count and timing. |

All clients upload the document as ~256 KiB `DocumentChunk`s with
`complete = true` on the last chunk, raise the gRPC receive-message limit
(page PNGs can exceed the 4 MiB default), and exit nonzero with
`gRPC error: <CODE>: <message>` on failure.

The server address defaults to `localhost:50053`; override with the
`GRLIBRE_ADDR` environment variable (the Python client also takes `--addr`).

## Prerequisite: a running server

```bash
GRLIBRE_TMPFS_DIR=/dev/shm GRLIBRE_PORT=50053 build/grlibre-server &
```

## Python (`clients/python/`)

```bash
cd clients/python
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
./generate.sh                                # generates stubs into gen/
.venv/bin/python client.py info
.venv/bin/python client.py pages ../../fixtures/sample3.docx out
.venv/bin/python client.py pdf ../../fixtures/sample3.docx out.pdf
```

## Node.js (`clients/node/`)

No codegen; the proto is loaded dynamically at startup.

```bash
cd clients/node
npm install
node client.js info
node client.js pages ../../fixtures/sample3.docx out
node client.js pdf ../../fixtures/sample3.docx out.pdf
```

## Java (`clients/java/`)

Gradle project using the protobuf plugin, pointed at the repo's `proto/`
tree. A wrapper pinned at Gradle 9.6.1 is included; a system `gradle`
(9.x) works identically.

```bash
cd clients/java
./gradlew run --args="info"
./gradlew run --args="pages ../../fixtures/sample3.docx out"
./gradlew run --args="pdf ../../fixtures/sample3.docx out.pdf"
```
