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

- `server.mjs`: Node BFF. Loads the proto dynamically at startup with
  `@grpc/proto-loader` (no codegen) and serves the static SPA from `public/`.
- `lib/`: query parsers and overlay math, imported by the BFF and the
  unit tests. No bundler.
- `public/`: single-page app, vanilla JS + CSS, no build step.
- `test/`: unit tests for `lib/` plus integration tests for the BFF
  (see Testing below).

### BFF endpoints

| Endpoint | Behavior |
|---|---|
| `GET /api/info` | `GetServiceInfo` as JSON. |
| `POST /api/render` | Raw file body in (filename via `X-Filename` header or `?filename=`); calls `StreamPages` and streams the response back as newline-delimited JSON. Each line is `{event, tMs, payloadBytes, data}` where `event` is the oneof field name (`documentInfo`, `pageImage`, `paragraph`, `sheetRow`, ..., `status`), `tMs` is milliseconds since the gRPC call started, and page PNGs are base64 in `data.png`. Heavy byte payloads on typed-content events (`embeddedImage.data`, `embeddedObject.replacementImage`) are replaced with their byte sizes. gRPC failures arrive as a final `{event: "error", data: {code, codeName, message}}` line. Optional `?parts=` takes a comma list of `DocumentPart` enum names (short `PAGES` or full `DOCUMENT_PART_PAGES`) and is sent as `StreamOptions` on the first upload chunk; the server then emits only those parts. Optional `?dpi=` (render DPI, server default 144, clamped server-side to 24-600) and `?firstPage=`/`?lastPage=` (1-based inclusive page-image range, 0/absent = unbounded) also ride `StreamOptions` on the first chunk; a page range restricts only page images: `documentInfo` keeps the full `pageCount`/`pageRects` and each `pageImage` keeps its document-absolute `index`. Optional `?format=` (`png`, `jpeg`, `webp`, or `svg`; default png) and `?quality=` (1-100, lossy formats only, server default 85) select the page image encoding; each `pageImage` names its actual encoding in `data.format` (`PAGE_IMAGE_FORMAT_*`) and the base64 bytes stay in `data.png` regardless of format. Optional `?grayscale=1`, `?maxWidth=`, `?timeout=`, `?trackedChanges=` (`as-is`/`final`/`original`/`markup`), `?skipHidden=1`, `?usedRange=1`, and `?notes=1` also ride `StreamOptions`. All of these are echoed in the `start` event alongside `parts`. Unknown part names, unknown `format` values, and non-integer or negative numeric params are rejected with HTTP 400; a backwards range or an out-of-range quality is forwarded and surfaces as the server's `INVALID_ARGUMENT` error event. |
| `POST /api/document` | Same upload and query options as `/api/render`; calls `ToDocument` and returns the mapped `Document` plus `documentInfo`/`status` as JSON. |
| `POST /api/pdf` | Same upload; calls `ConvertToPdf` and streams the PDF back as `application/pdf` with `Content-Disposition: attachment`. Optional `?firstPage=`/`?lastPage=`/`?timeout=`/`?trackedChanges=`/`?skipHidden=` ride the ConvertToPdf request. Errors before the first PDF byte return JSON with the gRPC status (`400` for `INVALID_ARGUMENT`, `413` for `RESOURCE_EXHAUSTED`, `502` otherwise). |
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

- `PORT`: HTTP port of the BFF (default `8080`; `0` picks an ephemeral port,
  reported on stdout).
- `GRLIBRE_ADDR`: gRPC target (default `localhost:50053`).

## Testing

```bash
npm run check      # syntax-check every JS entry (no gRPC needed)
npm run test:unit  # lib/ parsers + overlay math (no gRPC needed)
npm test           # unit + BFF integration (needs grlibre-server)
```

`test/options.test.mjs` and `test/overlay.test.mjs` cover the BFF query
parsers (`parts`, `format`, `grayscale`, `trackedChanges`, StreamOptions
and ConvertToPdf builders, `slimEvent`) and the lightbox twip-to-pixel math.

`test/bff.test.mjs` (node:test, no test framework dependencies) boots the BFF
as a child process on an ephemeral port and drives it over HTTP against the
real gRPC server on `localhost:50053`, which must be running. Covered:
`/api/info` shape; NDJSON event ordering (`documentInfo` first, `status` then
`end` last) and page count for `fixtures/sample3.docx`; part selection
(`parts=PAGES` emits pages but no typed content, `parts=PARAGRAPHS` the
reverse, `LINE_RECTS` attaches line rectangles, unknown names get HTTP 400);
render DPI (`dpi=72` reports 72 in every `pageImage` and exactly halves the
default 144-dpi pixel width); page ranges (`firstPage=2&lastPage=2` emits one
`pageImage` with document-absolute index 1 while `documentInfo` keeps the
full 4-page count and rects; a backwards range surfaces the server's
`INVALID_ARGUMENT` as the NDJSON error event; non-numeric `dpi` and negative
`firstPage` get HTTP 400); page image formats (`format=webp`/`format=jpeg`
emit pages with the right magic bytes and `PAGE_IMAGE_FORMAT_*` label,
`format=gif` gets HTTP 400, `quality=101` surfaces the server's
`INVALID_ARGUMENT`); spreadsheet `sheet`/`sheetRow` events; grayscale / SVG / max-width pages;
start-event echo of skipHidden / usedRange / notes / timeout / trackedChanges;
pageRect x dpi/1440 matching painted pixels and line boxes staying on-page;
`/api/document` mapping (and HTTP 400 on an unknown format); PDF magic
bytes plus a 1:1 page-range PDF; unknown-extension errors on both endpoints
(NDJSON `INVALID_ARGUMENT` / HTTP 400) including repeated PDF failures not
crashing the BFF; the fixtures listing and byte serving with traversal
rejection; `GET /` and static asset MIME types.

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

# render DPI override: every pageImage reports dpi 72
curl -sN -X POST --data-binary @../fixtures/sample3.docx \
  "http://localhost:8080/api/render?filename=sample3.docx&parts=PAGES&dpi=72" \
  | grep -o '"dpi":[0-9]*' | sort | uniq -c

# page range: one pageImage (document-absolute index 1) of a 4-page doc
curl -sN -X POST --data-binary @../fixtures/sample3.docx \
  "http://localhost:8080/api/render?filename=sample3.docx&parts=PAGES&firstPage=2&lastPage=2" \
  | cut -d'"' -f4 | sort | uniq -c

# WebP page images: every pageImage names its encoding
curl -sN -X POST --data-binary @../fixtures/sample3.docx \
  "http://localhost:8080/api/render?filename=sample3.docx&parts=PAGES&format=webp&quality=60" \
  | grep -o '"format":"[A-Z_]*"' | sort | uniq -c

# PDF conversion: bytes start with %PDF
curl -s -X POST --data-binary @../fixtures/sample3.docx \
  -H "X-Filename: sample3.docx" -o out.pdf http://localhost:8080/api/pdf
head -c 8 out.pdf   # -> %PDF-1.7

# fixture listing for the speed test
curl -s http://localhost:8080/api/fixtures
```

## UI features

Service info chips (LibreOffice version, per-document byte cap, format
count, render DPI) come from `/api/info`, and the accepted extensions are
listed in the upload zone. Upload is drag-and-drop or click-to-browse. The
document card (filename, resolved format, document class, page count)
appears the moment `DocumentInfo` arrives, and page thumbnails pop into the
grid as each `PageImage` streams in, without waiting for the full response.

A collapsible "Parts" panel below the upload zone carries one toggle chip
per `DocumentPart` (Pages, Metadata, Paragraphs, Tables, Images, Footnotes,
Headers/footers, Page styles, Indexes, Sheets, Slides, Shapes, Text frames,
Embedded objects, Line rects, Cell line rects, Comments, Tracked changes,
Bookmarks, Form fields). The default selection matches the server's
empty-list behavior (everything except cell line rects) and sends no
parameter; any other selection is sent explicitly as `?parts=`. Deselecting
Pages, for example, streams typed content with no thumbnails.

A compact render-options row below the Parts panel applies to the next
upload (and to re-renders of the same file) on `/api/render` only; PDF
conversion ignores it and the UI never sends it there. DPI is a segmented
control (Default 144, 72, 96, 192, 300) plus a free number entry that
overrides the presets; the server clamps to 24-600 and each thumbnail
caption shows the DPI the page actually rendered at. The page range is an
optional "from"/"to" pair (blank means all pages); a backwards pair is
swapped client-side (the server would reject it) with a note in the row.
Thumbnails keep their true document page numbers ("p.2", "p.3" for range
2-3), and the pages stat reads as emitted-vs-total, for example "2 of 4
(range 2-3)". The lightbox overlay is unaffected by a range: page rects come
from `DocumentInfo`, which a range never restricts, and both are keyed by
document-absolute page index. Format is a segmented control (PNG, JPEG,
WebP) plus a quality entry (1-100, enabled for the lossy formats, server
default 85); thumbnails and the lightbox pick their data-URL mime type from
each `pageImage`'s reported `format`, so lossy renders display natively.

A tabbed typed-content viewer sits below the thumbnails and fills
incrementally as events arrive; only tabs that received content appear. The
Text tab shows paragraphs in reading order with heading levels from
`outline_level`, list indent from `list_level`, styled runs (bold, italic,
underline, strikethrough), and hyperlinks as real links, with
footnotes/endnotes and comments (author, date, anchored text) in trailing
sections. The Tables tab renders each `TableData` as an HTML table on its
base grid, listing off-grid (split/merged) cells by office cell name. The
Sheets tab shows per-sheet grids from `SheetRow` events with column letters
and row numbers, display strings shown and formulas on hover, capped at the
first 200 used rows and 40 columns per sheet. The Slides tab lists each
slide's shapes with placeholder roles, outline-indented text paragraphs,
and speaker-notes shapes flagged. The Metadata tab shows `DocumentMetadata`
fields, statistics, and user properties.

Clicking a thumbnail opens a full-size lightbox with prev/next (arrow keys
work); the "Overlay" button (or `o`) draws the per-line bounding boxes
carried by paragraph/table/image events over the page image. Boxes are
document-absolute twips, mapped page-local by subtracting the page's
`PageRect` origin and scaled with `px = twips * dpi / 1440` into the PNG's
pixel space (an SVG viewBox, so display scaling is free). The overlay
requires the Line rects part (on by default).

The speed test (header nav) picks fixtures served by the BFF (default all),
iterations (default 2), and modes (pages-only as `parts=PAGES`, full, and
pdf), then runs them sequentially through the live endpoints. Measured in
the browser: time to first page event, total stream time, pages/sec, and
bytes received. Results land in a table plus a horizontal bar chart of
average total time per file/mode, drawn without a chart library.

Live stats show time to first page, total time, pages/sec, bytes received,
and typed-content event counts by kind (paragraphs, tables, sheet rows,
slides, comments, ...). "Download as PDF" re-uploads the same file to
`/api/pdf` with its own elapsed-time and size readout. A collapsible render
status panel carries warnings; gRPC failures show the status name (for
example `INVALID_ARGUMENT`) and the server's message.
