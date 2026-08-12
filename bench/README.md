# bench — speed test for the render service

Measures per-document latency and whole-service throughput against a running
`grlibre-server`, using the documents in `../fixtures/`.

```bash
../fixtures/fetch.sh          # once: download/generate the fixture set
./run.sh                      # venv + stub generation + full run
./run.sh --iterations 5 --concurrency 1,2,4,8 --json results.json
./run.sh --files ../fixtures/sample4.docx --modes pdf
./run.sh --modes pages-only --dpi 72   # per-request DPI override
./run.sh --modes pages-only --pages 100 --files ../fixtures/sample4.docx
./test.sh                     # unit tests (no server needed)
```

`--pages` exercises the page-range feature: on the 224-page sample4.docx,
`--pages 100` returns that single page in ~650 ms where the full paint takes
~14 s — the load and layout are paid once, the other 223 paints never happen.

Three modes per file:

- `pages-only` — `StreamPages` with `parts=[DOCUMENT_PART_PAGES]`: pure
  render speed, no typed-content extraction work.
- `pages-full` — `StreamPages` with no selection: render plus the full typed
  extraction (paragraphs, tables, sheets, slides, comments, ...).
- `pdf` — `ConvertToPdf`.

Reported per file: time to `DocumentInfo` (ttfb), time to first page/PDF
chunk (ttfp), median total wall time, pages/sec, output size. The
concurrency sweep then replays the whole fixture set at each level and
reports docs/sec and pages/sec; expect throughput to plateau at the server's
`GRLIBRE_MAX_CONCURRENT_DOCUMENTS` (workers in flight), which is the knob to
raise for a bigger box.

Sample numbers (Ryzen-class dev box, LibreOffice 26.2, DPI 144,
`GRLIBRE_MAX_CONCURRENT_DOCUMENTS=2`):

| document | mode | pages | ttfp | total | pages/s |
|---|---|---|---|---|---|
| sample3.docx (34 KiB) | pages-only | 4 | 229 ms | 333 ms | 12.0 |
| sample3.docx | pdf | 4 | 211 ms | 230 ms | 17.4 |
| sample4.docx (13.5 MiB) | pages-only | 224 | 689 ms | 14.0 s | 16.0 |
| sample4.docx | pdf | 224 | 1.55 s | 1.59 s | 140.6 |

Concurrency sweep on the same box: 0.56 docs/s at 1, 1.03 docs/s at 2, flat
at 4 (server capped at 2 workers).
