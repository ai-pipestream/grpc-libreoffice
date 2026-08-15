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
import { expectedPagePixels, boxInsidePage } from "../lib/overlay.mjs";

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

test("GET / serves the SPA and static assets with the right MIME", async () => {
  const html = await fetch(`${base}/`);
  assert.strictEqual(html.status, 200);
  assert.match(html.headers.get("content-type"), /text\/html/);
  const body = await html.text();
  assert.match(body, /grlibre/);
  assert.match(body, /opt-grayscale/);

  const js = await fetch(`${base}/app.js`);
  assert.strictEqual(js.status, 200);
  assert.match(js.headers.get("content-type"), /javascript/);

  const css = await fetch(`${base}/style.css`);
  assert.strictEqual(css.status, 200);
  assert.match(css.headers.get("content-type"), /text\/css/);
});

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
  assert.strictEqual(info.documentMapping, true);
  assert.strictEqual(info.packageRepair, true);
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

test("dpi=72 reports dpi 72 and halves the default pixel width", async () => {
  const defEvents = await renderEvents(DOCX, "sample3.docx", "&parts=PAGES");
  const lowEvents = await renderEvents(
    DOCX, "sample3.docx", "&parts=PAGES&dpi=72");

  const start = lowEvents[0];
  assert.strictEqual(start.event, "start");
  assert.strictEqual(start.data.dpi, 72, "start event echoes dpi");

  const defPages = defEvents.filter((e) => e.event === "pageImage");
  const lowPages = lowEvents.filter((e) => e.event === "pageImage");
  assert.ok(defPages.length > 0);
  assert.strictEqual(lowPages.length, defPages.length);
  lowPages.forEach((p, i) => {
    assert.strictEqual(p.data.dpi, 72, `page ${i} rendered at 72 dpi`);
    assert.strictEqual(
      p.data.widthPx * 2, defPages[i].data.widthPx,
      `page ${i} width must be exactly half the default-dpi width`);
  });
});

test("firstPage=2&lastPage=2 emits only page index 1, full pageCount", async () => {
  const events = await renderEvents(
    DOCX, "sample3.docx", "&parts=PAGES&firstPage=2&lastPage=2");

  const start = events[0];
  assert.strictEqual(start.data.firstPage, 2, "start event echoes firstPage");
  assert.strictEqual(start.data.lastPage, 2, "start event echoes lastPage");

  const info = events.find((e) => e.event === "documentInfo").data;
  assert.strictEqual(info.pageCount, 4,
    "DocumentInfo keeps the FULL page count despite the range");
  assert.strictEqual(info.pageRects.length, 4,
    "DocumentInfo keeps every page rect despite the range");

  const pages = events.filter((e) => e.event === "pageImage");
  assert.strictEqual(pages.length, 1, "exactly one page image in range 2:2");
  assert.strictEqual(pages[0].data.index, 1,
    "the emitted page keeps its document-absolute index");
  assert.strictEqual(events[events.length - 2].event, "status");
});

test("backwards range (firstPage=3&lastPage=2) surfaces INVALID_ARGUMENT", async () => {
  const events = await renderEvents(
    DOCX, "sample3.docx", "&firstPage=3&lastPage=2");
  const err = events.find((e) => e.event === "error");
  assert.ok(err, "an error event must be emitted");
  assert.strictEqual(err.data.codeName, "INVALID_ARGUMENT");
});

test("format=webp emits WebP pages that name their encoding", async () => {
  const events = await renderEvents(
    DOCX, "sample3.docx", "&parts=PAGES&format=webp&quality=60");
  const pages = events.filter((e) => e.event === "pageImage");
  assert.ok(pages.length > 0);
  for (const p of pages) {
    assert.strictEqual(p.data.format, "PAGE_IMAGE_FORMAT_WEBP");
    const bytes = Buffer.from(p.data.png, "base64");
    assert.strictEqual(bytes.subarray(0, 4).toString("latin1"), "RIFF");
    assert.strictEqual(bytes.subarray(8, 12).toString("latin1"), "WEBP");
  }
  assert.strictEqual(events[events.length - 2].event, "status");
});

test("format=jpeg emits JPEG pages", async () => {
  const events = await renderEvents(
    DOCX, "sample3.docx", "&parts=PAGES&format=jpeg");
  const pages = events.filter((e) => e.event === "pageImage");
  assert.ok(pages.length > 0);
  for (const p of pages) {
    assert.strictEqual(p.data.format, "PAGE_IMAGE_FORMAT_JPEG");
    const bytes = Buffer.from(p.data.png, "base64");
    assert.strictEqual(bytes[0], 0xff);
    assert.strictEqual(bytes[1], 0xd8);
  }
});

test("unknown format is rejected with HTTP 400", async () => {
  const body = fs.readFileSync(DOCX);
  const resp = await fetch(
    `${base}/api/render?filename=sample3.docx&format=gif`,
    { method: "POST", body });
  assert.strictEqual(resp.status, 400);
  const payload = await resp.json();
  assert.match(payload.error.message, /unknown format/);
});

test("quality over 100 surfaces INVALID_ARGUMENT from the server", async () => {
  const events = await renderEvents(
    DOCX, "sample3.docx", "&format=jpeg&quality=101");
  const error = events.find((e) => e.event === "error");
  assert.ok(error, "expected an error event");
  assert.strictEqual(error.data.codeName, "INVALID_ARGUMENT");
  assertAlive();
});

test("non-numeric dpi is rejected with HTTP 400", async () => {
  const resp = await fetch(
    `${base}/api/render?filename=sample3.docx&dpi=abc`,
    { method: "POST", body: fs.readFileSync(DOCX) });
  assert.strictEqual(resp.status, 400);
  const payload = await resp.json();
  assert.match(payload.error.message, /invalid dpi/);
});

test("negative firstPage is rejected with HTTP 400", async () => {
  const resp = await fetch(
    `${base}/api/render?filename=sample3.docx&firstPage=-1`,
    { method: "POST", body: fs.readFileSync(DOCX) });
  assert.strictEqual(resp.status, 400);
  const payload = await resp.json();
  assert.match(payload.error.message, /invalid firstPage/);
});

test("spreadsheet render emits sheet and sheetRow events", async () => {
  const events = await renderEvents(XLSX, "sample3.xlsx");
  assert.ok(events.some((e) => e.event === "sheet"));
  assert.ok(events.some((e) => e.event === "sheetRow"));
});

test("grayscale=1 still emits PNG page images", async () => {
  const events = await renderEvents(
    DOCX, "sample3.docx", "&parts=PAGES&grayscale=1");
  const pages = events.filter((e) => e.event === "pageImage");
  assert.ok(pages.length >= 1);
  const png = Buffer.from(pages[0].data.png, "base64");
  assert.strictEqual(png.subarray(0, 4).toString("binary"), "\x89PNG");
});

test("format=svg emits SVG page images", async () => {
  const events = await renderEvents(
    DOCX, "sample3.docx", "&parts=PAGES&format=svg");
  const pages = events.filter((e) => e.event === "pageImage");
  assert.ok(pages.length >= 1);
  assert.strictEqual(pages[0].data.format, "PAGE_IMAGE_FORMAT_SVG");
  const svg = Buffer.from(pages[0].data.png, "base64").toString("utf8");
  assert.match(svg, /<svg/);
});

test("maxWidth=200 bounds the painted page width", async () => {
  const events = await renderEvents(
    DOCX, "sample3.docx", "&parts=PAGES&maxWidth=200");
  const pages = events.filter((e) => e.event === "pageImage");
  assert.ok(pages.length >= 1);
  assert.ok(pages[0].data.widthPx > 0 && pages[0].data.widthPx <= 200,
    `width ${pages[0].data.widthPx} must fit in 200px`);
});

test("start event echoes skipHidden, usedRange, notes, timeout, trackedChanges", async () => {
  const events = await renderEvents(
    DOCX, "sample3.docx",
    "&parts=PAGES&skipHidden=1&usedRange=1&notes=1&timeout=45&trackedChanges=final");
  const start = events[0];
  assert.strictEqual(start.event, "start");
  assert.strictEqual(start.data.skipHidden, true);
  assert.strictEqual(start.data.usedRange, true);
  assert.strictEqual(start.data.notes, true);
  assert.strictEqual(start.data.timeout, 45);
  assert.strictEqual(start.data.trackedChanges, "TRACKED_CHANGE_DISPLAY_FINAL");
  assert.strictEqual(events[events.length - 2].event, "status");
});

test("pageRect twips scaled by dpi/1440 match painted pixels", async () => {
  const events = await renderEvents(
    DOCX, "sample3.docx", "&parts=PAGES,PARAGRAPHS,LINE_RECTS");
  const info = events.find((e) => e.event === "documentInfo").data;
  const pages = events.filter((e) => e.event === "pageImage");
  assert.ok(pages.length >= 1);
  for (const p of pages) {
    const rect = info.pageRects[p.data.index];
    assert.ok(rect, `page ${p.data.index} has a pageRect`);
    const expect = expectedPagePixels(rect, p.data.dpi);
    assert.ok(Math.abs(p.data.widthPx - expect.width) <= 1,
      `page ${p.data.index} width ${p.data.widthPx} vs expected ${expect.width}`);
    assert.ok(Math.abs(p.data.heightPx - expect.height) <= 1,
      `page ${p.data.index} height ${p.data.heightPx} vs expected ${expect.height}`);
  }
  const withRects = events.filter(
    (e) => e.event === "paragraph" && (e.data.lineRects || []).length > 0);
  assert.ok(withRects.length > 0, "lineRects present for overlay check");
  for (const e of withRects) {
    for (const box of e.data.lineRects) {
      const rect = info.pageRects[box.pageIndex];
      if (!rect) continue;
      assert.ok(boxInsidePage(box, rect),
        `line box on page ${box.pageIndex} stays inside its pageRect`);
    }
  }
});

test("non-numeric timeout and maxWidth are rejected with HTTP 400", async () => {
  for (const [qs, needle] of [
    ["timeout=abc", /invalid timeout/],
    ["maxWidth=-4", /invalid maxWidth/],
  ]) {
    const resp = await fetch(
      `${base}/api/render?filename=sample3.docx&${qs}`,
      { method: "POST", body: fs.readFileSync(DOCX) });
    assert.strictEqual(resp.status, 400, qs);
    const payload = await resp.json();
    assert.match(payload.error.message, needle);
  }
});

test("unknown trackedChanges value is rejected with HTTP 400", async () => {
  const resp = await fetch(
    `${base}/api/render?filename=sample3.docx&trackedChanges=bogus`,
    { method: "POST", body: fs.readFileSync(DOCX) });
  assert.strictEqual(resp.status, 400);
  const payload = await resp.json();
  assert.match(payload.error.message, /unknown trackedChanges/);
});

test("non-boolean grayscale is rejected with HTTP 400", async () => {
  const resp = await fetch(
    `${base}/api/render?filename=sample3.docx&grayscale=maybe`,
    { method: "POST", body: fs.readFileSync(DOCX) });
  assert.strictEqual(resp.status, 400);
  const payload = await resp.json();
  assert.match(payload.error.message, /invalid grayscale/);
});

test("POST /api/document returns a mapped Document", async () => {
  const resp = await fetch(
    `${base}/api/document?filename=sample3.docx&parts=PARAGRAPHS`,
    { method: "POST", body: fs.readFileSync(DOCX) });
  assert.strictEqual(resp.status, 200);
  const body = await resp.json();
  assert.ok(body.document);
  assert.ok(body.documentInfo);
  assert.strictEqual(body.documentInfo.sourceFormat, "docx");
});

test("POST /api/document with default parts embeds no page images", async () => {
  const resp = await fetch(
    `${base}/api/document?filename=sample3.docx`,
    { method: "POST", body: fs.readFileSync(DOCX) });
  assert.strictEqual(resp.status, 200);
  const body = await resp.json();
  assert.ok(body.document.texts?.length > 0, "typed content still mapped");
  for (const page of Object.values(body.document.pages ?? {})) {
    assert.ok(!page.image?.uri, "no page image data URIs by default");
  }
});

test("POST /api/document unknown format is HTTP 400", async () => {
  const resp = await fetch(
    `${base}/api/document?filename=nope.zzz-not-a-format`,
    { method: "POST", body: Buffer.from("garbage") });
  assert.strictEqual(resp.status, 400);
  const payload = await resp.json();
  assert.strictEqual(payload.error.codeName, "INVALID_ARGUMENT");
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

test("POST /api/pdf?firstPage=1&lastPage=1 still returns a PDF", async () => {
  const resp = await fetch(
    `${base}/api/pdf?filename=sample3.docx&firstPage=1&lastPage=1`,
    { method: "POST", body: fs.readFileSync(DOCX) });
  assert.strictEqual(resp.status, 200);
  const bytes = Buffer.from(await resp.arrayBuffer());
  assert.strictEqual(bytes.subarray(0, 4).toString(), "%PDF");
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
