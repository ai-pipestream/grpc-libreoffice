# Node.js example client

Uses `@grpc/grpc-js` with `@grpc/proto-loader`: the proto file is loaded
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
node client.js pages ../../fixtures/sample3.docx out --format webp --quality 60
node client.js pages ../../fixtures/sample3.docx out --first-page 2 --last-page 2 --parts PAGES
node client.js pdf ../../fixtures/sample3.docx out.pdf
node client.js pdf ../../fixtures/sample3.docx out.pdf --redact 0:120
```

`pages` flags map onto `StreamOptions` on the first upload chunk:

- `--dpi <n>`: `render_dpi` (server clamps to [24,600]; 0 or omitted = server default)
- `--first-page` / `--last-page`: 1-based inclusive page-image range
- `--format png|jpeg|webp|svg` and `--quality <n>`: page encoding (files use `.png` / `.jpg` / `.webp` / `.svg` from `PageImage.format`)
- `--parts PAGES,PARAGRAPHS,...`: short or `DOCUMENT_PART_*` names
- `--max-width`, `--grayscale`, `--timeout`, `--tracked-changes`, `--skip-hidden`, `--used-range`, `--notes`, `--form`, `--redact`, `--repair`

Each page line prints the DPI and encoding the page actually rendered at.

Server address: `GRLIBRE_ADDR` environment variable
(default `localhost:50053`).
