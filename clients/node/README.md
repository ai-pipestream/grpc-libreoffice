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
node client.js pages ../../fixtures/sample3.docx out --dpi 72
node client.js pdf ../../fixtures/sample3.docx out.pdf
```

`pages --dpi <n>` requests a render DPI (`StreamOptions.render_dpi`, sent on
the first upload chunk; the server clamps it to [24,600], 0 or omitted means
the server default). Each page line prints the DPI the page actually
rendered at.

Server address: `GRLIBRE_ADDR` environment variable
(default `localhost:50053`).
