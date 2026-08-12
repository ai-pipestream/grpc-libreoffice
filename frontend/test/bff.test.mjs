// BFF integration tests: boots server.mjs as a child process on an
// ephemeral port and drives it over HTTP against the real gRPC server on
// localhost:50053 (which must be running).
//
//   npm test
import { test, before, after } from "node:test";
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import path from "node:path";
import fs from "node:fs";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const SERVER = path.join(__dirname, "..", "server.mjs");
const FIXTURES = path.join(__dirname, "..", "..", "fixtures");
const DOCX = path.join(FIXTURES, "sample3.docx");
const XLSX = path.join(FIXTURES, "sample3.xlsx");

let child;
let base; // http://localhost:<port>

before(async () => {
  assert.ok(fs.existsSync(DOCX), `missing fixture ${DOCX}`);
  child = spawn(process.execPath, [SERVER], {
    env: { ...process.env, PORT: "0" },
    stdio: ["ignore", "pipe", "inherit"],
  });
  base = await new Promise((resolve, reject) => {
    let out = "";
    const timer = setTimeout(
      () => reject(new Error("BFF did not report its port in 10s")), 10000);
    child.stdout.on("data", (buf) => {
      out += buf.toString();
      const m = out.match(/listening on (http:\/\/localhost:\d+)/);
      if (m) {
        clearTimeout(timer);
        resolve(m[1]);
      }
    });
    child.on("exit", (code) => {
      clearTimeout(timer);
      reject(new Error(`BFF exited early with code ${code}`));
    });
  });
});

after(() => {
  if (child) child.kill("SIGTERM");
});

function assertAlive() {
  assert.strictEqual(child.exitCode, null, "BFF process died");
}

// POSTs a file to an NDJSON endpoint and returns the parsed event lines.
async function renderEvents(file, filename, query = "") {
  const body = fs.readFileSync(file);
  const resp = await fetch(
    `${base}/api/render?filename=${encodeURIComponent(filename)}${query}`,
    { method: "POST", body });
  assert.strictEqual(resp.status, 200);
  const text = await resp.text();
  return text.trim().split("\n").map((l) => JSON.parse(l));
}

test("GET /api/info reports service capabilities", async () => {
  const resp = await fetch(`${base}/api/info`);
  assert.strictEqual(resp.status, 200);
  const info = await resp.json();
  assert.strictEqual(typeof info.serviceVersion, "string");
  assert.strictEqual(info.apiVersion, "v1");
  assert.ok(Array.isArray(info.supportedFormats));
  assert.ok(info.supportedFormats.includes("docx"));
  assert.ok(Number(info.maxDocumentBytes) > 0);
  assert.strictEqual(typeof info.typedContent, "boolean");
});

test("POST /api/render streams ordered NDJSON for sample3.docx", async () => {
  const events = await renderEvents(DOCX, "sample3.docx");

  assert.strictEqual(events[0].event, "start");
  assert.strictEqual(
    events[1].event, "documentInfo",
    "first gRPC event must be documentInfo");
  const last = events[events.length - 1];
  const secondLast = events[events.length - 2];
  assert.strictEqual(last.event, "end");
  assert.strictEqual(secondLast.event, "status");
  assert.strictEqual(secondLast.data.state, "STATE_OK");

  const info = events[1].data;
  assert.ok(info.pageCount >= 1);
  const pages = events.filter((e) => e.event === "pageImage");
  assert.strictEqual(
    pages.length, info.pageCount,
    "one pageImage per documentInfo.pageCount");
  // Page order and PNG payloads.
  pages.forEach((p, i) => {
    assert.strictEqual(p.data.index, i);
    assert.ok(p.data.png.length > 0, "pageImage carries base64 PNG");
    assert.ok(p.data.widthPx > 0 && p.data.heightPx > 0 && p.data.dpi > 0);
  });
  // A docx has body paragraphs.
  assert.ok(events.some((e) => e.event === "paragraph"));
});

test("parts=PAGES emits pageImage but no typed content", async () => {
  const events = await renderEvents(DOCX, "sample3.docx", "&parts=PAGES");
  assert.ok(events.some((e) => e.event === "pageImage"));
  assert.ok(!events.some((e) => e.event === "paragraph"),
    "paragraphs must not stream when only PAGES is selected");
  assert.ok(!events.some((e) => e.event === "metadata"));
  assert.strictEqual(events[events.length - 2].event, "status");
});

test("parts=PARAGRAPHS emits paragraphs but no pageImage", async () => {
  const events = await renderEvents(DOCX, "sample3.docx", "&parts=PARAGRAPHS");
  assert.ok(events.some((e) => e.event === "paragraph"));
  assert.ok(!events.some((e) => e.event === "pageImage"),
    "pages must not stream when only PARAGRAPHS is selected");
});

test("parts=PARAGRAPHS,LINE_RECTS attaches line rectangles", async () => {
  const events = await renderEvents(
    DOCX, "sample3.docx", "&parts=PARAGRAPHS,LINE_RECTS");
  const withRects = events.filter(
    (e) => e.event === "paragraph" && (e.data.lineRects || []).length > 0);
  assert.ok(withRects.length > 0,
    "at least one paragraph must carry lineRects");
  const box = withRects[0].data.lineRects[0];
  assert.strictEqual(typeof box.xTwips, "number");
  assert.strictEqual(typeof box.yTwips, "number");
  assert.ok(box.widthTwips > 0 && box.heightTwips > 0);
  assert.ok(box.pageIndex >= 0);
});

test("full-name parts (DOCUMENT_PART_PAGES) are accepted", async () => {
  const events = await renderEvents(
    DOCX, "sample3.docx", "&parts=DOCUMENT_PART_PAGES");
  assert.ok(events.some((e) => e.event === "pageImage"));
  assert.ok(!events.some((e) => e.event === "paragraph"));
});

test("unknown part name is rejected with HTTP 400", async () => {
  const resp = await fetch(
    `${base}/api/render?filename=sample3.docx&parts=NOT_A_PART`,
    { method: "POST", body: fs.readFileSync(DOCX) });
  assert.strictEqual(resp.status, 400);
  const payload = await resp.json();
  assert.match(payload.error.message, /unknown document part/);
});

test("spreadsheet render emits sheet and sheetRow events", async () => {
  const events = await renderEvents(XLSX, "sample3.xlsx");
  assert.ok(events.some((e) => e.event === "sheet"));
  assert.ok(events.some((e) => e.event === "sheetRow"));
});

test("POST /api/pdf returns a PDF (magic bytes)", async () => {
  const resp = await fetch(
    `${base}/api/pdf?filename=sample3.docx`,
    { method: "POST", body: fs.readFileSync(DOCX) });
  assert.strictEqual(resp.status, 200);
  assert.strictEqual(resp.headers.get("content-type"), "application/pdf");
  const bytes = Buffer.from(await resp.arrayBuffer());
  assert.strictEqual(bytes.subarray(0, 4).toString(), "%PDF");
  assert.ok(bytes.length > 1000);
});

test("unknown extension: render yields NDJSON INVALID_ARGUMENT", async () => {
  const events = await renderEvents(DOCX, "sample3.zzz-not-a-format");
  const err = events.find((e) => e.event === "error");
  assert.ok(err, "an error event must be emitted");
  assert.strictEqual(err.data.codeName, "INVALID_ARGUMENT");
  assert.strictEqual(events[events.length - 1].event, "error",
    "error must terminate the stream");
});

test("unknown extension: pdf yields HTTP 400, repeatedly, without crashing", async () => {
  for (let i = 0; i < 3; i++) {
    const resp = await fetch(
      `${base}/api/pdf?filename=nope.zzz-not-a-format`,
      { method: "POST", body: Buffer.from("garbage") });
    assert.strictEqual(resp.status, 400, `attempt ${i + 1}`);
    const payload = await resp.json();
    assert.strictEqual(payload.error.codeName, "INVALID_ARGUMENT");
    assertAlive();
  }
  // The BFF must still serve after repeated error responses
  // (regression: ERR_HTTP_HEADERS_SENT).
  const info = await fetch(`${base}/api/info`);
  assert.strictEqual(info.status, 200);
});

test("GET /api/fixtures lists fixture files with sizes", async () => {
  const resp = await fetch(`${base}/api/fixtures`);
  assert.strictEqual(resp.status, 200);
  const { files } = await resp.json();
  assert.ok(Array.isArray(files));
  const docx = files.find((f) => f.name === "sample3.docx");
  assert.ok(docx, "sample3.docx must be listed");
  assert.strictEqual(docx.bytes, fs.statSync(DOCX).size);
  for (const f of files) {
    assert.ok(!f.name.includes("/"), "no directories in the listing");
  }
});

test("GET /api/fixtures/<name> serves the bytes", async () => {
  const resp = await fetch(`${base}/api/fixtures/sample3.docx`);
  assert.strictEqual(resp.status, 200);
  const bytes = Buffer.from(await resp.arrayBuffer());
  assert.strictEqual(bytes.length, fs.statSync(DOCX).size);
});

test("GET /api/fixtures/<traversal> is rejected", async () => {
  const resp = await fetch(`${base}/api/fixtures/..%2Fserver.mjs`);
  assert.ok(resp.status === 400 || resp.status === 404);
});
