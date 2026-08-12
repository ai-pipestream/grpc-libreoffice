# Node.js example client

Uses `@grpc/grpc-js` with `@grpc/proto-loader` — the proto file is loaded
dynamically from `../../proto` at startup, so there is no codegen step.

## Setup

```bash
npm install
```

## Usage

```bash
node client.js info
node client.js pages ../../fixtures/sample3.docx out
node client.js pdf ../../fixtures/sample3.docx out.pdf
```

Server address: `GRLIBRE_ADDR` environment variable
(default `localhost:50053`).
