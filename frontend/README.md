# grlibre demo frontend

A demo web UI for the `OfficeRenderService` gRPC server: drop an office
document, watch its pages stream in as PNGs one by one, inspect typed-content
counts and render timings, and download the same document as a PDF.

## Architecture

Browsers cannot speak native gRPC, so a small Node.js BFF bridges the two:

```
browser  --HTTP/NDJSON-->  server.mjs (BFF, :8080)  --gRPC-->  grlibre-server (:50053)
```

- `server.mjs` — Node BFF. Loads the proto dynamically at startup with
  `@grpc/proto-loader` (no codegen) and serves the static SPA from `public/`.
- `public/` — single-page app, vanilla JS + CSS, no build step.

### BFF endpoints

| Endpoint | Behavior |
|---|---|
| `GET /api/info` | `GetServiceInfo` as JSON. |
| `POST /api/render` | Raw file body in (filename via `X-Filename` header or `?filename=`); calls `StreamPages` and streams the response back as newline-delimited JSON. Each line is `{event, tMs, payloadBytes, data}` where `event` is the oneof field name (`documentInfo`, `pageImage`, `paragraph`, `sheetRow`, ..., `status`), `tMs` is milliseconds since the gRPC call started, and page PNGs are base64 in `data.png`. Heavy byte payloads on typed-content events (`embeddedImage.data`, `embeddedObject.replacementImage`) are replaced with their byte sizes. gRPC failures arrive as a final `{event: "error", data: {code, codeName, message}}` line. |
| `POST /api/pdf` | Same upload; calls `ConvertToPdf` and streams the PDF back as `application/pdf` with `Content-Disposition: attachment`. Errors before the first PDF byte return JSON with the gRPC status (`400` for `INVALID_ARGUMENT`, `413` for `RESOURCE_EXHAUSTED`, `502` otherwise). |

## Running

Prerequisites: Node.js (tested with v24), and the gRPC server listening
(default `localhost:50053`):

```bash
# from the repo root, if the server is not already running
GRLIBRE_TMPFS_DIR=/dev/shm GRLIBRE_PORT=50053 build/grlibre-server &
```

Then:

```bash
cd frontend
npm install
npm start          # http://localhost:8080
```

Environment variables:

- `PORT` — HTTP port of the BFF (default `8080`).
- `GRLIBRE_ADDR` — gRPC target (default `localhost:50053`).

## Verifying from the command line

```bash
# service info
curl -s http://localhost:8080/api/info | head -c 300

# progressive render: NDJSON events ending in a status line
curl -sN -X POST --data-binary @../fixtures/sample3.docx \
  -H "X-Filename: sample3.docx" http://localhost:8080/api/render | tail -n 3

# PDF conversion: bytes start with %PDF
curl -s -X POST --data-binary @../fixtures/sample3.docx \
  -H "X-Filename: sample3.docx" -o out.pdf http://localhost:8080/api/pdf
head -c 8 out.pdf   # -> %PDF-1.7
```

## UI features

- Service info chips (LibreOffice version, per-document byte cap, format
  count, render DPI) fetched from `/api/info`; accepted extensions listed in
  the upload zone.
- Drag-and-drop or click-to-browse upload; the document card (filename,
  resolved format, document class, page count) appears the moment
  `DocumentInfo` arrives, and page thumbnails pop into the grid as each
  `PageImage` streams in — no waiting for the full response.
- Click a thumbnail for a full-size lightbox with prev/next (arrow keys work).
- Live stats: time to first page, total time, pages/sec, bytes received, and
  typed-content event counts by kind (paragraphs, tables, sheet rows,
  slides, comments, ...).
- "Download as PDF" re-uploads the same file to `/api/pdf` with its own
  elapsed-time and size readout.
- Collapsible render status panel with warnings; gRPC failures show the
  status name (for example `INVALID_ARGUMENT`) and the server's message.
