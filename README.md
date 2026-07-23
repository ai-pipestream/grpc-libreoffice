# grpc-libreoffice

A C++ gRPC server that renders office documents through LibreOfficeKit.
Clients stream document bytes in and receive either every page as a PNG
image or the document as a PDF, streamed back. Pages render directly from
the office core's tile painter; no intermediate PDF exists on the page path.

The server exists for three reasons:

1. LibreOffice is a desktop process, not a service. LibreOfficeKit embeds the
   office core, and this server puts that behind gRPC with a stable wire
   contract.
2. Crash isolation. Every document is rendered by its own short-lived worker
   process that loads the core, renders, and exits. A crash or hang in the
   core kills one worker; the server maps it to a gRPC status and moves on.
3. Document bytes should not touch persistent disk. All writable paths (the
   uploaded document, the office user profile, PDF output) live under a
   memory-backed tmpfs; the container runs with a read-only root filesystem.

## API

`ai.pipestream.office.v1.OfficeRenderService` (see `proto/`, linted with buf
at STANDARD plus COMMENTS):

- `StreamPages(stream StreamPagesRequest) returns (stream StreamPagesResponse)`.
  The client streams the document as chunks, marking the last one `complete`.
  The server responds with `DocumentInfo` (resolved format, page count,
  document class), then one `PageImage` per page in page order (PNG, with
  pixel dimensions and effective DPI), then typed content events, then one
  final `RenderStatus`. Spreadsheets emit one image per sheet, presentations
  one per slide.

  Typed content comes from the same loaded document the pages were painted
  from, in one pass with no second conversion: `DocumentMetadata` (title,
  author, subject, keywords, numeric timestamps) for every document type,
  and for text documents `Paragraph` events (style, outline level, list
  level, exact caret positions in twips from the live layout, runs with
  font, size, weight, slant, underline, strikethrough, color), `TableData`
  events (grid dimensions and named, addressed cells), and `EmbeddedImage`
  events (original or re-encoded bytes, anchor position, laid-out size),
  `Footnote` events (label, anchor, content runs), `HeaderFooter` content
  per page style, `PageStyleInfo` (page size, margins, columns, in twips),
  `DocumentIndex` events (tables of contents and other generated
  indexes), `TextFrame` events (styled runs, layout anchor, laid-out size,
  and text-chain names), and `Shape` events for text-bearing draw-page
  shapes, imported textboxes included. Drawing documents emit `DrawingShape` events (shape type, name,
  position and size in twips, rotation, group nesting, text runs) in
  page-then-paint order, plus `EmbeddedImage` events for image shapes.
  Embedded objects emit `EmbeddedObject` events for every document class:
  charts with typed numeric series, categories, titles, and a tabular
  projection walked from the live chart model; embedded spreadsheets as a
  used-range cell grid; Math formulas as their StarMath command; and other
  OLE payloads with their replacement graphic, geometry, and class id.
  Spreadsheets emit one `Sheet` header per sheet (name, visibility, tab
  color, used bounds, print areas) followed by `SheetRow` events carrying
  only the used range's non-empty typed cells (cell type, formula, numeric
  value, display string, number-format key and code, merge span), plus
  `SheetCellComment`, `SheetChart`, and `SheetPivotTable` events per sheet
  and `SheetNamedRange` events per workbook.
  Presentations emit one `Slide` header per slide (name, autolayout,
  master page) followed by `SlideShape` events in paint order, each with
  its placeholder role, geometry in twips, and text paragraphs carrying
  outline depth, plus the speaker-notes shape of each notes page. Body
  runs carry character offsets in a documented annotation text space, so
  standoff annotations (NLP spans) anchor to the stream directly.
  For text documents, `Paragraph`, `TableData`, and `EmbeddedImage` events
  additionally carry true per-line bounding rectangles (`LineBox`, in
  document-absolute twips) measured from the same layout the pages were
  painted from: a wrapping or page-straddling paragraph yields one box per
  laid-out line in reading order.
  Every event is emitted the moment it exists, so a caller can process page
  images while typed content is still streaming. Extraction problems degrade
  to `RenderStatus.warnings`, never a failed render.

  A request can select which parts are emitted through `StreamOptions`
  (`DocumentPart` values, page images included). An empty selection means
  every part, and the work behind an unselected part is skipped, not just
  its emission. `DocumentInfo` and `RenderStatus` are always sent.
  `DocumentInfo` also carries the layout rectangle of every page in the
  same twips space the typed positions use, so a consumer can map any
  document-absolute position to page-local coordinates.
- `ConvertToPdf`: same upload contract; the response streams the PDF as
  ordered chunks instead of page images.
- `GetServiceInfo`: versions, limits, and accepted source formats, for
  orchestrators and tool facades.

The source format resolves from the filename extension first and the content
type second; unresolvable documents are rejected with `INVALID_ARGUMENT`.
Accepted formats cover the Word, Excel, and PowerPoint families (modern and
legacy), the OpenDocument families, RTF, CSV, HTML, and plain text.

Errors are gRPC status codes: `INVALID_ARGUMENT` (no bytes, missing complete
flag, unresolvable format, or the core cannot load the document),
`RESOURCE_EXHAUSTED` (over the byte cap), `DEADLINE_EXCEEDED` (per-document
timeout, worker killed), `INTERNAL` (worker crash). Health checking and
reflection are registered.

Accepted formats also include PDF, which the core imports through Draw;
PDF pages rasterize like any other document and, because the import
produces a drawing model, emit `DrawingShape` typed content.

The repo also carries `ai.pipestream.document.v1`, the typed document
structure schema (tracking docling-core v2 for interoperability), and a
consumer-side mapper (`src/docling_map.h`, built into the server library)
that folds a `StreamPages` event stream into one such `Document`: items in
typed arenas linked by JSON Pointer refs, groups per sheet, slide, frame,
and drawing group, headers and footers as furniture, speaker notes on the
notes layer, and per-line page-local bounding boxes as provenance. The
mapper never touches LibreOffice and builds a valid document from any part
selection.

## Process model

The server buffers each upload under a hard byte cap, then spawns
`grlibre-worker` with the document on stdin. The worker initializes
LibreOfficeKit with its own user profile, loads the document, and writes
length-prefixed response events to stdout, which the server relays to the
gRPC stream as they arrive. A concurrency gate bounds simultaneous workers;
a deadline kills workers that hang. Worker exit codes distinguish "could not
load the document" (client error) from crashes (server error).

## Configuration

| Variable | Default | Meaning |
|---|---|---|
| `GRLIBRE_PORT` | `50053` | Listen port |
| `GRLIBRE_MAX_DOCUMENT_MIB` | `100` | Per-document byte cap |
| `GRLIBRE_MAX_CONCURRENT_DOCUMENTS` | `2` | Worker processes in flight |
| `GRLIBRE_TASK_TIMEOUT_SECONDS` | `120` | Per-document deadline |
| `GRLIBRE_RENDER_DPI` | `144` | Requested page render DPI |
| `GRLIBRE_MAX_PAGE_PIXELS` | `4096` | Per-side pixel bound; pages downscale to fit |
| `GRLIBRE_LO_PATH` | `/usr/lib/libreoffice/program` | LibreOffice installation |
| `GRLIBRE_METRICS_INTERVAL_SECONDS` | `60` | Metrics line interval, `0` disables |

## Build and run

Requires cmake, a C++17 toolchain, LibreOfficeKit headers
(`libreofficekit-dev`), the LibreOffice SDK (`libreoffice-dev`, for the
typed content extraction that attaches to the in-process UNO model), and
LibreOffice itself at runtime. gRPC is fetched and built by CMake; UNO type
headers are generated by cppumaker during the build.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
ctest --test-dir build --output-on-failure

docker build -t grlibre .
docker run --rm --read-only --tmpfs /tmp:rw,size=1g -p 50053:50053 grlibre
```

The image build runs the full test suite, including real renders through a
headless LibreOffice, before an image can exist. Tests author their fixtures
in memory; the render tests skip cleanly on machines without LibreOffice.
