# grlibre demo frontend

A demo web UI for the `OfficeRenderService` gRPC server: drop an office
document, watch its pages stream in as PNGs one by one, read the extracted
typed content live, inspect per-line layout boxes over the rendered pages,
and benchmark the service from the browser.

## Architecture

Browsers cannot speak native gRPC, so a small Node.js BFF bridges the two:

```
browser  --HTTP/NDJSON-->  server.mjs (BFF, :8080)  --gRPC-->  grlibre-server (:50053)
```

- `server.mjs` — Node BFF. Loads the proto dynamically at startup with
  `@grpc/proto-loader` (no codegen) and serves the static SPA from `public/`.
- `public/` — single-page app, vanilla JS + CSS, no build step.
- `test/` — integration tests for the BFF (see Testing below).

### BFF endpoints

| Endpoint | Behavior |
|---|---|
| `GET /api/info` | `GetServiceInfo` as JSON. |
| `POST /api/render` | Raw file body in (filename via `X-Filename` header or `?filename=`); calls `StreamPages` and streams the response back as newline-delimited JSON. Each line is `{event, tMs, payloadBytes, data}` where `event` is the oneof field name (`documentInfo`, `pageImage`, `paragraph`, `sheetRow`, ..., `status`), `tMs` is milliseconds since the gRPC call started, and page PNGs are base64 in `data.png`. Heavy byte payloads on typed-content events (`embeddedImage.data`, `embeddedObject.replacementImage`) are replaced with their byte sizes. gRPC failures arrive as a final `{event: "error", data: {code, codeName, message}}` line. Optional `?parts=` takes a comma list of `DocumentPart` enum names (short `PAGES` or full `DOCUMENT_PART_PAGES`) and is sent as `StreamOptions` on the first upload chunk; the server then emits only those parts. Unknown part names are rejected with HTTP 400. |
| `POST /api/pdf` | Same upload; calls `ConvertToPdf` and streams the PDF back as `application/pdf` with `Content-Disposition: attachment`. Errors before the first PDF byte return JSON with the gRPC status (`400` for `INVALID_ARGUMENT`, `413` for `RESOURCE_EXHAUSTED`, `502` otherwise). |
| `GET /api/fixtures` | Lists the sample documents in `../fixtures` as `{files: [{name, bytes}]}` (regular files only). |
| `GET /api/fixtures/<name>` | Serves one fixture's bytes, so the browser speed test can upload them back through the render endpoints. |

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

- `PORT` — HTTP port of the BFF (default `8080`; `0` picks an ephemeral port,
  reported on stdout).
- `GRLIBRE_ADDR` — gRPC target (default `localhost:50053`).

## Testing

```bash
npm test
```

`test/bff.test.mjs` (node:test, no test framework dependencies) boots the BFF
as a child process on an ephemeral port and drives it over HTTP against the
real gRPC server on `localhost:50053`, which must be running. Covered:
`/api/info` shape; NDJSON event ordering (`documentInfo` first, `status` then
`end` last) and page count for `fixtures/sample3.docx`; part selection
(`parts=PAGES` emits pages but no typed content, `parts=PARAGRAPHS` the
reverse, `LINE_RECTS` attaches line rectangles, unknown names get HTTP 400);
spreadsheet `sheet`/`sheetRow` events; PDF magic bytes; unknown-extension
errors on both endpoints (NDJSON `INVALID_ARGUMENT` / HTTP 400) including
repeated PDF failures not crashing the BFF; the fixtures listing and byte
serving with traversal rejection.

`test/check-overlay-math.mjs` is a manual sanity script for the lightbox
overlay: it prints page-rect-to-pixel scaling and line-box bounds for a
rendered docx (`node test/check-overlay-math.mjs`).

## Verifying from the command line

```bash
# service info
curl -s http://localhost:8080/api/info | head -c 300

# progressive render: NDJSON events ending in a status line
curl -sN -X POST --data-binary @../fixtures/sample3.docx \
  -H "X-Filename: sample3.docx" http://localhost:8080/api/render | tail -n 3

# part selection: pages only, no typed content events
curl -sN -X POST --data-binary @../fixtures/sample3.docx \
  "http://localhost:8080/api/render?filename=sample3.docx&parts=PAGES" \
  | cut -d'"' -f4 | sort | uniq -c

# PDF conversion: bytes start with %PDF
curl -s -X POST --data-binary @../fixtures/sample3.docx \
  -H "X-Filename: sample3.docx" -o out.pdf http://localhost:8080/api/pdf
head -c 8 out.pdf   # -> %PDF-1.7

# fixture listing for the speed test
curl -s http://localhost:8080/api/fixtures
```

## UI features

- Service info chips (LibreOffice version, per-document byte cap, format
  count, render DPI) fetched from `/api/info`; accepted extensions listed in
  the upload zone.
- Drag-and-drop or click-to-browse upload; the document card (filename,
  resolved format, document class, page count) appears the moment
  `DocumentInfo` arrives, and page thumbnails pop into the grid as each
  `PageImage` streams in — no waiting for the full response.
- **Parts selection**: a collapsible "Parts" panel below the upload zone with
  one toggle chip per `DocumentPart` (Pages, Metadata, Paragraphs, Tables,
  Images, Footnotes, Headers/footers, Page styles, Indexes, Sheets, Slides,
  Shapes, Text frames, Embedded objects, Line rects, Cell line rects,
  Comments, Tracked changes, Bookmarks, Form fields). The default selection
  matches the server's empty-list behavior (everything except cell line
  rects) and sends no parameter; any other selection is sent explicitly as
  `?parts=`. Deselecting Pages, for example, streams typed content with no
  thumbnails.
- **Typed-content viewer**: a tabbed panel below the thumbnails, filled
  incrementally as events arrive; only tabs that received content appear.
  - *Text* — paragraphs in reading order with heading levels from
    `outline_level`, list indent from `list_level`, styled runs (bold,
    italic, underline, strikethrough), and hyperlinks as real links;
    footnotes/endnotes and comments (author, date, anchored text) in
    trailing sections.
  - *Tables* — each `TableData` as an HTML table on its base grid, off-grid
    (split/merged) cells listed by office cell name.
  - *Sheets* — per-sheet grids from `SheetRow` events with column letters
    and row numbers, display strings shown, formulas on hover, capped at the
    first 200 used rows and 40 columns per sheet.
  - *Slides* — per-slide shape list with placeholder roles, outline-indented
    text paragraphs, and speaker-notes shapes flagged.
  - *Metadata* — `DocumentMetadata` fields, statistics, and user properties.
- **Layout overlay**: click a thumbnail for a full-size lightbox with
  prev/next (arrow keys work); the "Overlay" button (or `o`) draws the
  per-line bounding boxes carried by paragraph/table/image events over the
  page image. Boxes are document-absolute twips, mapped page-local by
  subtracting the page's `PageRect` origin and scaled with
  `px = twips * dpi / 1440` into the PNG's pixel space (an SVG viewBox, so
  display scaling is free). Requires the Line rects part (on by default).
- **Speed test** (header nav): pick fixtures served by the BFF (default all),
  iterations (default 2), and modes — pages-only (`parts=PAGES`), full, and
  pdf — then run them sequentially through the live endpoints. Measured in
  the browser: time to first page event, total stream time, pages/sec, and
  bytes received; results land in a table plus a horizontal bar chart of
  average total time per file/mode. No chart library.
- Live stats: time to first page, total time, pages/sec, bytes received, and
  typed-content event counts by kind (paragraphs, tables, sheet rows,
  slides, comments, ...).
- "Download as PDF" re-uploads the same file to `/api/pdf` with its own
  elapsed-time and size readout.
- Collapsible render status panel with warnings; gRPC failures show the
  status name (for example `INVALID_ARGUMENT`) and the server's message.
