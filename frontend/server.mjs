// grlibre demo BFF: bridges the browser (HTTP + NDJSON streaming) to the
// OfficeRenderService gRPC server. No codegen: proto-loader loads the proto
// dynamically at startup.
import http from "node:http";
import path from "node:path";
import fs from "node:fs";
import { fileURLToPath } from "node:url";
import grpc from "@grpc/grpc-js";
import protoLoader from "@grpc/proto-loader";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const PROTO_ROOT = path.join(__dirname, "..", "proto");
const PROTO_FILE = path.join(
  PROTO_ROOT, "ai", "pipestream", "office", "v1", "office_service.proto");
const PUBLIC_DIR = path.join(__dirname, "public");
const FIXTURES_DIR = path.join(__dirname, "..", "fixtures");

const PORT = Number(process.env.PORT || 8080);
const GRLIBRE_ADDR = process.env.GRLIBRE_ADDR || "localhost:50053";
const UPLOAD_CHUNK = 256 * 1024;

const packageDefinition = protoLoader.loadSync(PROTO_FILE, {
  keepCase: false,
  longs: Number,
  enums: String,
  bytes: String, // bytes fields become base64 strings in decoded messages
  defaults: true,
  oneofs: true, // adds virtual "event" property naming the set oneof field
  includeDirs: [PROTO_ROOT],
});
const officePkg =
  grpc.loadPackageDefinition(packageDefinition).ai.pipestream.office.v1;

const client = new officePkg.OfficeRenderService(
  GRLIBRE_ADDR,
  grpc.credentials.createInsecure(),
  {
    "grpc.max_receive_message_length": -1,
    "grpc.max_send_message_length": -1,
  });

const STATUS_NAMES = Object.fromEntries(
  Object.entries(grpc.status).map(([name, code]) => [code, name]));

// Valid DocumentPart enum names, read from the loaded proto so the list
// never drifts from the contract. Accepts short forms too: "PAGES" maps to
// "DOCUMENT_PART_PAGES".
const DOCUMENT_PART_NAMES = new Set(
  officePkg.DocumentPart.type.value.map((v) => v.name));

// Parses ?parts=PAGES,METADATA into full enum names, or null when absent.
// Throws on unknown names.
function parsePartsParam(url) {
  const raw = url.searchParams.get("parts");
  if (raw == null || raw.trim() === "") return null;
  const parts = [];
  for (const token of raw.split(",")) {
    const t = token.trim().toUpperCase();
    if (!t) continue;
    const full = t.startsWith("DOCUMENT_PART_") ? t : "DOCUMENT_PART_" + t;
    if (!DOCUMENT_PART_NAMES.has(full)) {
      throw new Error(`unknown document part "${token.trim()}"`);
    }
    parts.push(full);
  }
  return parts.length ? parts : null;
}

// Parses one optional non-negative integer query param, or null when
// absent. Throws on anything that is not a plain base-10 non-negative
// integer (fractions, negatives, exponents, garbage).
function parseNonNegativeIntParam(url, name) {
  const raw = url.searchParams.get(name);
  if (raw == null || raw.trim() === "") return null;
  const t = raw.trim();
  if (!/^\d+$/.test(t)) {
    throw new Error(`invalid ${name} "${raw}": must be a non-negative integer`);
  }
  return Number(t);
}

function grpcErrorPayload(err) {
  return {
    code: err.code ?? grpc.status.UNKNOWN,
    codeName: STATUS_NAMES[err.code] || "UNKNOWN",
    message: err.details || err.message || "unknown gRPC error",
  };
}

function readBody(req) {
  return new Promise((resolve, reject) => {
    const chunks = [];
    req.on("data", (c) => chunks.push(c));
    req.on("end", () => resolve(Buffer.concat(chunks)));
    req.on("error", reject);
  });
}

function resolveFilename(req, url) {
  const header = req.headers["x-filename"];
  const query = url.searchParams.get("filename");
  const raw = header || query || "document.bin";
  try {
    return decodeURIComponent(raw);
  } catch {
    return raw;
  }
}

// Streams the uploaded buffer into a gRPC client-streaming call as
// DocumentChunk messages, the last one marked complete. firstExtra is
// merged into the first request message (StreamOptions ride there: the
// server resolves the first non-empty parts list).
function sendUpload(call, buffer, filename, contentType, firstExtra) {
  let offset = 0;
  let first = true;
  while (offset < buffer.length) {
    const end = Math.min(offset + UPLOAD_CHUNK, buffer.length);
    const complete = end >= buffer.length;
    call.write({
      ...(first ? firstExtra : null),
      chunk: {
        documentId: "",
        filename: first ? filename : "",
        contentType: first ? contentType : "",
        data: buffer.subarray(offset, end),
        complete,
      },
    });
    first = false;
    offset = end;
  }
  if (buffer.length === 0) {
    call.write({
      ...firstExtra,
      chunk: { filename, contentType: contentType || "", data: Buffer.alloc(0), complete: true },
    });
  }
  call.end();
}

// Approximate decoded byte size of a base64 string.
function b64Size(s) {
  if (!s) return 0;
  let padding = 0;
  if (s.endsWith("==")) padding = 2;
  else if (s.endsWith("=")) padding = 1;
  return Math.floor((s.length * 3) / 4) - padding;
}

// Replaces heavyweight byte payloads in typed-content events with their
// sizes so the NDJSON stream stays light. Page PNGs are kept: the UI needs
// them. Returns the number of payload bytes represented by this event.
function slimEvent(kind, data) {
  let bytes = 0;
  if (kind === "pageImage") {
    bytes = b64Size(data.png);
  } else if (kind === "embeddedImage") {
    bytes = b64Size(data.data);
    data.dataBytes = bytes;
    delete data.data;
  } else if (kind === "embeddedObject") {
    bytes = b64Size(data.replacementImage);
    data.replacementImageBytes = bytes;
    delete data.replacementImage;
  }
  return bytes;
}

async function handleInfo(res) {
  client.GetServiceInfo({}, (err, info) => {
    if (err) {
      res.writeHead(502, { "Content-Type": "application/json" });
      res.end(JSON.stringify({ error: grpcErrorPayload(err) }));
      return;
    }
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end(JSON.stringify(info));
  });
}

// Maps the short ?format= names onto PageImageFormat enum names. Anything
// else is a 400.
const PAGE_FORMATS = {
  png: "PAGE_IMAGE_FORMAT_PNG",
  jpeg: "PAGE_IMAGE_FORMAT_JPEG",
  webp: "PAGE_IMAGE_FORMAT_WEBP",
};

function parseFormatParam(url) {
  const raw = url.searchParams.get("format");
  if (raw == null || raw.trim() === "") return null;
  const format = PAGE_FORMATS[raw.trim().toLowerCase()];
  if (!format) {
    throw new Error(`unknown format "${raw}": expected png, jpeg, or webp`);
  }
  return format;
}

async function handleRender(req, res, url) {
  const filename = resolveFilename(req, url);
  const contentType = req.headers["content-type"] || "";
  let parts, dpi, firstPage, lastPage, format, quality;
  try {
    parts = parsePartsParam(url);
    dpi = parseNonNegativeIntParam(url, "dpi");
    firstPage = parseNonNegativeIntParam(url, "firstPage");
    lastPage = parseNonNegativeIntParam(url, "lastPage");
    format = parseFormatParam(url);
    quality = parseNonNegativeIntParam(url, "quality");
  } catch (err) {
    res.writeHead(400, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ error: { message: String(err.message) } }));
    return;
  }
  const body = await readBody(req);

  res.writeHead(200, {
    "Content-Type": "application/x-ndjson; charset=utf-8",
    "Cache-Control": "no-cache",
    "X-Accel-Buffering": "no",
  });

  const t0 = Date.now();
  const emit = (obj) => res.write(JSON.stringify(obj) + "\n");
  emit({
    event: "start",
    tMs: 0,
    data: {
      filename, bytes: body.length, parts: parts || [],
      dpi, firstPage, lastPage, format, quality,
    },
  });

  const call = client.StreamPages();
  let ended = false;

  call.on("data", (msg) => {
    const kind = msg.event; // virtual oneof field name (camelCase)
    if (!kind) return;
    const data = msg[kind];
    const payloadBytes = slimEvent(kind, data);
    emit({ event: kind, tMs: Date.now() - t0, payloadBytes, data });
  });
  call.on("error", (err) => {
    if (ended) return;
    ended = true;
    emit({ event: "error", tMs: Date.now() - t0, data: grpcErrorPayload(err) });
    res.end();
  });
  call.on("end", () => {
    if (ended) return;
    ended = true;
    emit({ event: "end", tMs: Date.now() - t0 });
    res.end();
  });
  req.on("close", () => {
    if (!ended) call.cancel();
  });

  // StreamOptions on the first chunk. A backwards page range is forwarded
  // as-is: the server rejects it with INVALID_ARGUMENT, which surfaces as
  // the NDJSON error event.
  const options = {};
  if (parts) options.parts = parts;
  if (dpi != null) options.renderDpi = dpi;
  if (firstPage != null) options.firstPage = firstPage;
  if (lastPage != null) options.lastPage = lastPage;
  if (format != null) options.pageFormat = format;
  // An out-of-range quality is forwarded: the server's INVALID_ARGUMENT
  // surfaces as the NDJSON error event, like a backwards page range.
  if (quality != null) options.pageQuality = quality;
  sendUpload(call, body, filename, contentType,
    Object.keys(options).length ? { options } : null);
}

// Lists the demo fixture files (regular files only) with their sizes.
function handleFixtures(res) {
  fs.readdir(FIXTURES_DIR, { withFileTypes: true }, (err, entries) => {
    if (err) {
      res.writeHead(500, { "Content-Type": "application/json" });
      res.end(JSON.stringify({ error: { message: String(err.message) } }));
      return;
    }
    const files = entries
      .filter((e) => e.isFile() && !e.name.startsWith(".") && !e.name.endsWith(".sh"))
      .map((e) => {
        const st = fs.statSync(path.join(FIXTURES_DIR, e.name));
        return { name: e.name, bytes: st.size };
      })
      .sort((a, b) => a.name.localeCompare(b.name));
    res.writeHead(200, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ files }));
  });
}

// Serves one fixture file's bytes so the browser speed test can upload it
// back through /api/render and /api/pdf.
function handleFixtureFile(res, name) {
  const decoded = decodeURIComponent(name);
  if (decoded.includes("/") || decoded.includes("\\") || decoded.includes("..")) {
    res.writeHead(400, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ error: { message: "bad fixture name" } }));
    return;
  }
  const filePath = path.join(FIXTURES_DIR, decoded);
  fs.readFile(filePath, (err, contents) => {
    if (err) {
      res.writeHead(404, { "Content-Type": "application/json" });
      res.end(JSON.stringify({ error: { message: "fixture not found" } }));
      return;
    }
    res.writeHead(200, {
      "Content-Type": "application/octet-stream",
      "Content-Length": contents.length,
    });
    res.end(contents);
  });
}

async function handlePdf(req, res, url) {
  const filename = resolveFilename(req, url);
  const contentType = req.headers["content-type"] || "";
  const body = await readBody(req);

  const call = client.ConvertToPdf();
  let headersSent = false;
  let finished = false;
  const pdfName =
    (filename.replace(/\.[^.]*$/, "") || "document") + ".pdf";

  call.on("data", (msg) => {
    if (msg.event === "pdfChunk") {
      if (!headersSent) {
        headersSent = true;
        res.writeHead(200, {
          "Content-Type": "application/pdf",
          "Content-Disposition":
            `attachment; filename="${pdfName.replace(/["\\]/g, "_")}"`,
          "Cache-Control": "no-cache",
        });
      }
      res.write(Buffer.from(msg.pdfChunk.data, "base64"));
    }
  });
  call.on("error", (err) => {
    if (finished) return;
    finished = true;
    if (headersSent) {
      res.destroy();
      return;
    }
    const payload = grpcErrorPayload(err);
    const httpStatus =
      payload.codeName === "INVALID_ARGUMENT" ? 400 :
      payload.codeName === "RESOURCE_EXHAUSTED" ? 413 : 502;
    res.writeHead(httpStatus, { "Content-Type": "application/json" });
    res.end(JSON.stringify({ error: payload }));
  });
  call.on("end", () => {
    if (finished) return;
    finished = true;
    if (!headersSent) {
      res.writeHead(502, { "Content-Type": "application/json" });
      res.end(JSON.stringify({
        error: { codeName: "INTERNAL", message: "stream ended without PDF data" },
      }));
      return;
    }
    res.end();
  });
  req.on("close", () => {
    if (!res.writableEnded) call.cancel();
  });

  sendUpload(call, body, filename, contentType);
}

const MIME = {
  ".html": "text/html; charset=utf-8",
  ".css": "text/css; charset=utf-8",
  ".js": "text/javascript; charset=utf-8",
  ".svg": "image/svg+xml",
  ".png": "image/png",
  ".ico": "image/x-icon",
};

function serveStatic(res, urlPath) {
  const rel = urlPath === "/" ? "/index.html" : urlPath;
  const filePath = path.join(PUBLIC_DIR, path.normalize(rel));
  if (!filePath.startsWith(PUBLIC_DIR)) {
    res.writeHead(403);
    res.end("forbidden");
    return;
  }
  fs.readFile(filePath, (err, contents) => {
    if (err) {
      res.writeHead(404, { "Content-Type": "text/plain" });
      res.end("not found");
      return;
    }
    const ext = path.extname(filePath).toLowerCase();
    res.writeHead(200, { "Content-Type": MIME[ext] || "application/octet-stream" });
    res.end(contents);
  });
}

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, `http://${req.headers.host || "localhost"}`);
  try {
    if (req.method === "GET" && url.pathname === "/api/info") {
      await handleInfo(res);
    } else if (req.method === "GET" && url.pathname === "/api/fixtures") {
      handleFixtures(res);
    } else if (req.method === "GET" && url.pathname.startsWith("/api/fixtures/")) {
      handleFixtureFile(res, url.pathname.slice("/api/fixtures/".length));
    } else if (req.method === "POST" && url.pathname === "/api/render") {
      await handleRender(req, res, url);
    } else if (req.method === "POST" && url.pathname === "/api/pdf") {
      await handlePdf(req, res, url);
    } else if (req.method === "GET" || req.method === "HEAD") {
      serveStatic(res, url.pathname);
    } else {
      res.writeHead(405);
      res.end();
    }
  } catch (err) {
    if (!res.headersSent) {
      res.writeHead(500, { "Content-Type": "application/json" });
    }
    res.end(JSON.stringify({ error: { message: String(err?.message || err) } }));
  }
});

server.listen(PORT, () => {
  // The actual bound port matters when PORT=0 asks for an ephemeral one
  // (the test suite does); the log line is the contract the tests parse.
  console.log(
    `grlibre BFF listening on http://localhost:${server.address().port}`);
  console.log(`gRPC target: ${GRLIBRE_ADDR}`);
});
