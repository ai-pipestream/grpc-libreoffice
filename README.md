# grpc-libreoffice

A gRPC server that converts office documents to PDF through a pool of headless
LibreOffice processes. Clients stream document bytes in and receive the PDF
back as a byte stream.

The server exists for two reasons:

1. LibreOffice is a desktop process, not a service. Pooling it behind gRPC
   gives any client high-fidelity office rendering over a stable wire
   contract, and contains crashes: a wedged office process is killed and
   replaced without touching the server, and every process is recycled after a
   configured number of conversions.
2. Document bytes should not touch persistent disk. The office processes'
   working directory and user profile live on memory-backed tmp storage, and
   the container runs with a read-only root filesystem plus a tmpfs at /tmp.

## API

`ai.pipestream.office.v1.OfficeConvertService` (see `grlibre-api/src/main/proto`):

- `ConvertToPdf(stream ConvertRequestChunk) returns (stream ConvertEvent)`.
  The client streams the document as chunks, marking the last one `complete`.
  The server responds with `DocumentInfo` (the resolved source format), the
  PDF as ordered `PdfChunk` events, then one final `ConvertStatus` with byte
  counts and timing.
- `GetServiceInfo`: versions, limits, pool shape, and the source formats the
  engine accepts, for orchestrators and tool facades.

The source format resolves from the filename extension first and the content
type second; a document that resolves to neither is rejected with
`INVALID_ARGUMENT`. Supported formats are probed from the conversion engine's
registry at startup and include the Word, Excel, and PowerPoint families
(modern and legacy), the OpenDocument families, RTF, CSV, and plain text.

Errors are gRPC status codes: `INVALID_ARGUMENT` (no bytes, missing complete
flag, unresolvable format), `RESOURCE_EXHAUSTED` (over the byte cap),
`DEADLINE_EXCEEDED` (per-task conversion timeout), `UNAVAILABLE` (office pool
not running), `INTERNAL` (office process failed the conversion). Standard gRPC
health checking and reflection are registered.

## Process model

Each conversion runs on its own virtual thread and is handed to one office
process from the pool; a semaphore bounds how many buffered documents are in
flight at once. Office processes are single-tasked, recycled after
`GRLIBRE_MAX_TASKS_PER_PROCESS` conversions, and killed and restarted if a
task exceeds the timeout, so one poisoned document cannot degrade the pool.

## Configuration

| Variable | Default | Meaning |
|---|---|---|
| `GRLIBRE_PORT` | `50053` | Listen port |
| `GRLIBRE_MAX_DOCUMENT_MIB` | `100` | Per-document byte cap (`RESOURCE_EXHAUSTED` above it) |
| `GRLIBRE_POOL_SIZE` | `2` | Headless office processes in the pool |
| `GRLIBRE_MAX_TASKS_PER_PROCESS` | `50` | Conversions before a process is recycled |
| `GRLIBRE_TASK_TIMEOUT_SECONDS` | `120` | Per-conversion timeout (`DEADLINE_EXCEEDED` above it) |
| `GRLIBRE_MAX_CONCURRENT_CONVERSIONS` | pool size | Buffered conversions in flight before queueing |
| `GRLIBRE_METRICS_INTERVAL_SECONDS` | `60` | Metrics line interval, `0` disables |

## Build and run

```bash
./gradlew build          # protocol tests always; real-conversion tests when soffice is on PATH
./gradlew :grlibre-service:run

docker build -t grlibre .
docker run --rm --read-only --tmpfs /tmp:rw,size=1g -p 50053:50053 grlibre
```

The image build installs LibreOffice in the build stage and runs the full test
suite, including real conversions, before an image can exist. The runtime
image needs the tmpfs because LibreOffice requires a writable working
directory and user profile; everything else is read-only.

Tests author their fixtures in memory (a minimal DOCX is assembled from its
XML parts at test time). No binary files are committed.
