#!/usr/bin/env node
// Example CLI client for the grpc-libreoffice OfficeRenderService.
//
//   node client.js info
//   node client.js pages <file> [outdir] [--dpi N] [--first-page N]
//        [--last-page N] [--format png|jpeg|webp] [--quality N]
//        [--parts PAGES,PARAGRAPHS,...]
//   node client.js pdf <file> [out.pdf]
//
// Server address defaults to localhost:50053; override with GRLIBRE_ADDR.

import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { randomUUID } from "node:crypto";
import grpc from "@grpc/grpc-js";
import protoLoader from "@grpc/proto-loader";

const HERE = path.dirname(fileURLToPath(import.meta.url));
const PROTO_ROOT = path.resolve(HERE, "../../proto");
const CHUNK_SIZE = 256 * 1024;
// Page PNGs and embedded images can exceed gRPC's 4 MiB default.
const MAX_MESSAGE_BYTES = 128 * 1024 * 1024;

const FORMAT_ENUM = {
  png: "PAGE_IMAGE_FORMAT_PNG",
  jpeg: "PAGE_IMAGE_FORMAT_JPEG",
  jpg: "PAGE_IMAGE_FORMAT_JPEG",
  webp: "PAGE_IMAGE_FORMAT_WEBP",
};

const packageDefinition = protoLoader.loadSync(
  path.join(PROTO_ROOT, "ai/pipestream/office/v1/office_service.proto"),
  {
    includeDirs: [PROTO_ROOT],
    keepCase: true,
    longs: Number,
    enums: String,
    defaults: true,
    oneofs: true,
  },
);
const officeV1 = grpc.loadPackageDefinition(packageDefinition).ai.pipestream
  .office.v1;

const DOCUMENT_PART_NAMES = new Set(
  officeV1.DocumentPart?.type?.value?.map((v) => v.name)
    ?? Object.keys(officeV1.DocumentPart || {}).filter((k) =>
      k.startsWith("DOCUMENT_PART_")),
);

function makeClient() {
  const addr = process.env.GRLIBRE_ADDR || "localhost:50053";
  return new officeV1.OfficeRenderService(
    addr,
    grpc.credentials.createInsecure(),
    { "grpc.max_receive_message_length": MAX_MESSAGE_BYTES },
  );
}

function fail(err) {
  const code = grpc.status[err.code] ?? err.code;
  console.error(`gRPC error: ${code}: ${err.details || err.message}`);
  process.exit(1);
}

function parsePositiveInt(flag, raw) {
  const n = Number.parseInt(raw, 10);
  if (!Number.isInteger(n) || n <= 0) {
    console.error(`${flag} needs a positive integer`);
    process.exit(2);
  }
  return n;
}

function parseParts(raw) {
  if (!raw) return [];
  const parts = [];
  for (const token of raw.split(",")) {
    const t = token.trim().toUpperCase();
    if (!t) continue;
    const full = t.startsWith("DOCUMENT_PART_") ? t : "DOCUMENT_PART_" + t;
    if (!DOCUMENT_PART_NAMES.has(full)) {
      console.error(
        `unknown part ${JSON.stringify(token)}: expected a DocumentPart name ` +
          `(PAGES or DOCUMENT_PART_PAGES)`,
      );
      process.exit(2);
    }
    parts.push(full);
  }
  return parts;
}

function streamOptions(opts) {
  const parts = parseParts(opts.parts);
  const format = opts.format ? FORMAT_ENUM[opts.format] : null;
  if (!format && opts.format) {
    console.error("--format must be png, jpeg, or webp");
    process.exit(2);
  }
  if (
    !opts.dpi &&
    !opts.firstPage &&
    !opts.lastPage &&
    !format &&
    !opts.quality &&
    parts.length === 0
  ) {
    return null;
  }
  const options = {};
  if (opts.dpi) options.render_dpi = opts.dpi;
  if (opts.firstPage) options.first_page = opts.firstPage;
  if (opts.lastPage) options.last_page = opts.lastPage;
  if (format) options.page_format = format;
  if (opts.quality) options.page_quality = opts.quality;
  if (parts.length) options.parts = parts;
  return options;
}

function pageExt(format) {
  if (format === "PAGE_IMAGE_FORMAT_JPEG") return "jpg";
  if (format === "PAGE_IMAGE_FORMAT_WEBP") return "webp";
  return "png";
}

function pageLabel(format) {
  if (format === "PAGE_IMAGE_FORMAT_JPEG") return "jpeg";
  if (format === "PAGE_IMAGE_FORMAT_WEBP") return "webp";
  return "png";
}

// Writes `file` into the call as 256 KiB DocumentChunks, last one complete.
// `firstExtra` fields ride the first request only (per-request options
// resolve to the first value seen in the upload stream).
function uploadFile(call, file, firstExtra = {}) {
  const data = fs.readFileSync(file);
  const total = data.length;
  let offset = 0;
  let first = true;
  do {
    const end = Math.min(offset + CHUNK_SIZE, total);
    const chunk = {
      data: data.subarray(offset, end),
      complete: end === total,
    };
    const req = { chunk };
    if (first) {
      chunk.document_id = randomUUID();
      chunk.filename = path.basename(file);
      Object.assign(req, firstExtra);
      first = false;
    }
    call.write(req);
    offset = end;
  } while (offset < total);
  call.end();
}

function printDocumentInfo(info) {
  console.log(`document_id : ${info.document_id}`);
  console.log(`format      : ${info.source_format}`);
  console.log(`type        : ${info.document_type}`);
  console.log(`pages       : ${info.page_count}`);
}

function printRenderStatus(status, totalMs) {
  console.log(`status      : ${status.state}`);
  console.log(`input bytes : ${status.input_bytes.toLocaleString()}`);
  console.log(`output bytes: ${status.output_bytes.toLocaleString()}`);
  console.log(
    `render time : ${status.render_millis} ms (server) / ${totalMs.toFixed(0)} ms (wall)`,
  );
  for (const w of status.warnings) console.log(`warning     : ${w}`);
}

function cmdInfo() {
  const client = makeClient();
  client.GetServiceInfo({}, (err, resp) => {
    if (err) fail(err);
    console.log(`service version     : ${resp.service_version}`);
    console.log(`libreoffice version : ${resp.libreoffice_version}`);
    console.log(`api version         : ${resp.api_version}`);
    console.log(
      `max document bytes  : ${resp.max_document_bytes.toLocaleString()}`,
    );
    console.log(`max concurrent docs : ${resp.max_concurrent_documents}`);
    console.log(`render dpi          : ${resp.render_dpi}`);
    console.log(`typed content       : ${resp.typed_content}`);
    console.log(`diskless documents  : ${resp.diskless_documents}`);
    console.log(`supported formats   : ${resp.supported_formats.join(", ")}`);
  });
}

function cmdPages(file, outdir, opts) {
  fs.mkdirSync(outdir, { recursive: true });
  const client = makeClient();
  const t0 = performance.now();
  let firstPageMs = null;
  let pages = 0;
  const counts = new Map();
  const options = streamOptions(opts);

  const call = client.StreamPages();
  call.on("data", (resp) => {
    switch (resp.event) {
      case "document_info":
        printDocumentInfo(resp.document_info);
        break;
      case "page_image": {
        if (firstPageMs === null) firstPageMs = performance.now() - t0;
        const img = resp.page_image;
        const name = `page-${String(img.index + 1).padStart(4, "0")}.${pageExt(img.format)}`;
        fs.writeFileSync(path.join(outdir, name), img.png);
        pages += 1;
        console.log(
          `  ${name}  ${img.width_px}x${img.height_px}px @ ${img.dpi} dpi  ` +
            `${pageLabel(img.format)}  ${img.png.length.toLocaleString()} bytes`,
        );
        break;
      }
      case "status": {
        const totalMs = performance.now() - t0;
        console.log();
        if (counts.size > 0) {
          const width = Math.max(...[...counts.keys()].map((k) => k.length));
          console.log("typed content events:");
          for (const kind of [...counts.keys()].sort()) {
            console.log(`  ${kind.padEnd(width)}  ${counts.get(kind)}`);
          }
          console.log();
        }
        printRenderStatus(resp.status, totalMs);
        if (firstPageMs !== null)
          console.log(`first page  : ${firstPageMs.toFixed(0)} ms`);
        console.log(`pages saved : ${pages} -> ${outdir}/`);
        break;
      }
      default:
        counts.set(resp.event, (counts.get(resp.event) ?? 0) + 1);
    }
  });
  call.on("error", fail);
  uploadFile(call, file, options ? { options } : {});
}

function cmdPdf(file, out = "out.pdf") {
  const client = makeClient();
  const t0 = performance.now();
  let total = 0;
  const fd = fs.openSync(out, "w");

  const call = client.ConvertToPdf();
  call.on("data", (resp) => {
    switch (resp.event) {
      case "document_info":
        printDocumentInfo(resp.document_info);
        break;
      case "pdf_chunk":
        fs.writeSync(fd, resp.pdf_chunk.data);
        total += resp.pdf_chunk.data.length;
        break;
      case "status":
        printRenderStatus(resp.status, performance.now() - t0);
        break;
    }
  });
  call.on("end", () => {
    fs.closeSync(fd);
    console.log(`wrote       : ${total.toLocaleString()} bytes -> ${out}`);
  });
  call.on("error", (err) => {
    fs.closeSync(fd);
    fail(err);
  });
  uploadFile(call, file);
}

const PAGES_USAGE =
  "usage: node client.js pages <file> [outdir] [--dpi <n>] " +
  "[--first-page <n>] [--last-page <n>] [--format png|jpeg|webp] " +
  "[--quality <n>] [--parts PAGES,PARAGRAPHS,...]";

function parsePagesArgs(rest) {
  const opts = {
    dpi: 0,
    firstPage: 0,
    lastPage: 0,
    format: "",
    quality: 0,
    parts: "",
  };
  const positional = [];
  let i = 0;
  const need = (flag) => {
    if (i + 1 >= rest.length) {
      console.error(`${flag} needs a value`);
      process.exit(2);
    }
    return rest[++i];
  };
  for (; i < rest.length; i++) {
    const a = rest[i];
    switch (a) {
      case "--dpi":
        opts.dpi = parsePositiveInt("--dpi", need("--dpi"));
        break;
      case "--first-page":
        opts.firstPage = parsePositiveInt("--first-page", need("--first-page"));
        break;
      case "--last-page":
        opts.lastPage = parsePositiveInt("--last-page", need("--last-page"));
        break;
      case "--format":
        opts.format = need("--format").toLowerCase();
        if (!(opts.format in FORMAT_ENUM)) {
          console.error("--format must be png, jpeg, or webp");
          process.exit(2);
        }
        break;
      case "--quality":
        opts.quality = parsePositiveInt("--quality", need("--quality"));
        break;
      case "--parts":
        opts.parts = need("--parts");
        break;
      default:
        if (a.startsWith("-")) {
          console.error(`unknown flag ${a}`);
          process.exit(2);
        }
        positional.push(a);
    }
  }
  return { positional, opts };
}

const [cmd, ...rest] = process.argv.slice(2);
switch (cmd) {
  case "info":
    cmdInfo();
    break;
  case "pages": {
    const { positional, opts } = parsePagesArgs(rest);
    if (!positional[0]) {
      console.error(PAGES_USAGE);
      process.exit(2);
    }
    cmdPages(positional[0], positional[1] || "pages-out", opts);
    break;
  }
  case "pdf":
    if (!rest[0]) {
      console.error("usage: node client.js pdf <file> [out.pdf]");
      process.exit(2);
    }
    cmdPdf(rest[0], rest[1]);
    break;
  default:
    console.error(
      "usage: node client.js <info | pages <file> [outdir] [options] | pdf <file> [out.pdf]>",
    );
    console.error(PAGES_USAGE);
    process.exit(2);
}
