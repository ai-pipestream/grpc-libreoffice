// Unit tests for BFF query parsers and StreamOptions builders.
// No HTTP, no gRPC — `npm run test:unit` is enough.
import { test } from "node:test";
import assert from "node:assert/strict";
import {
  PAGE_FORMATS,
  TRACKED_CHANGES,
  parsePartsParam,
  parseNonNegativeIntParam,
  parseFormatParam,
  parseBoolParam,
  parseTrackedParam,
  parseRenderQuery,
  buildStreamOptions,
  buildPdfExtras,
  b64Size,
  slimEvent,
} from "../lib/options.mjs";

const PARTS = new Set([
  "DOCUMENT_PART_PAGES",
  "DOCUMENT_PART_PARAGRAPHS",
  "DOCUMENT_PART_LINE_RECTS",
  "DOCUMENT_PART_METADATA",
]);

function url(qs) {
  return new URL("http://localhost/api/render" + (qs ? "?" + qs : ""));
}

test("parsePartsParam accepts short and full names, skips empties", () => {
  assert.deepEqual(
    parsePartsParam(url("parts=PAGES,PARAGRAPHS"), PARTS),
    ["DOCUMENT_PART_PAGES", "DOCUMENT_PART_PARAGRAPHS"]);
  assert.deepEqual(
    parsePartsParam(url("parts=DOCUMENT_PART_PAGES"), PARTS),
    ["DOCUMENT_PART_PAGES"]);
  assert.deepEqual(
    parsePartsParam(url("parts=PAGES,,LINE_RECTS,"), PARTS),
    ["DOCUMENT_PART_PAGES", "DOCUMENT_PART_LINE_RECTS"]);
  assert.equal(parsePartsParam(url(""), PARTS), null);
  assert.equal(parsePartsParam(url("parts="), PARTS), null);
  assert.equal(parsePartsParam(url("parts=,,,"), PARTS), null);
});

test("parsePartsParam rejects unknown names", () => {
  assert.throws(
    () => parsePartsParam(url("parts=NOT_A_PART"), PARTS),
    /unknown document part/);
  assert.throws(
    () => parsePartsParam(url("parts=PAGES,BOGUS"), PARTS),
    /unknown document part "BOGUS"/);
});

test("parseNonNegativeIntParam accepts digits only", () => {
  assert.equal(parseNonNegativeIntParam(url("dpi=72"), "dpi"), 72);
  assert.equal(parseNonNegativeIntParam(url("dpi=0"), "dpi"), 0);
  assert.equal(parseNonNegativeIntParam(url(""), "dpi"), null);
  assert.equal(parseNonNegativeIntParam(url("dpi="), "dpi"), null);
  assert.throws(() => parseNonNegativeIntParam(url("dpi=abc"), "dpi"), /invalid dpi/);
  assert.throws(() => parseNonNegativeIntParam(url("dpi=-1"), "dpi"), /invalid dpi/);
  assert.throws(() => parseNonNegativeIntParam(url("dpi=1.5"), "dpi"), /invalid dpi/);
  assert.throws(() => parseNonNegativeIntParam(url("dpi=1e2"), "dpi"), /invalid dpi/);
  assert.throws(() => parseNonNegativeIntParam(url("dpi=%2B3"), "dpi"), /invalid dpi/);
});

test("parseFormatParam maps short names including svg", () => {
  assert.equal(parseFormatParam(url("format=png")), PAGE_FORMATS.png);
  assert.equal(parseFormatParam(url("format=JPEG")), PAGE_FORMATS.jpeg);
  assert.equal(parseFormatParam(url("format=WebP")), PAGE_FORMATS.webp);
  assert.equal(parseFormatParam(url("format=svg")), PAGE_FORMATS.svg);
  assert.equal(parseFormatParam(url("")), null);
  assert.throws(() => parseFormatParam(url("format=gif")), /unknown format/);
});

test("parseBoolParam accepts 1/true/yes and 0/false/no", () => {
  assert.equal(parseBoolParam(url("grayscale=1"), "grayscale"), true);
  assert.equal(parseBoolParam(url("grayscale=true"), "grayscale"), true);
  assert.equal(parseBoolParam(url("grayscale=YES"), "grayscale"), true);
  assert.equal(parseBoolParam(url("grayscale=0"), "grayscale"), false);
  assert.equal(parseBoolParam(url("grayscale=false"), "grayscale"), false);
  assert.equal(parseBoolParam(url("grayscale=no"), "grayscale"), false);
  assert.equal(parseBoolParam(url(""), "grayscale"), null);
  assert.throws(
    () => parseBoolParam(url("grayscale=maybe"), "grayscale"),
    /invalid grayscale/);
});

test("parseTrackedParam maps aliases including show-markup", () => {
  assert.equal(parseTrackedParam(url("trackedChanges=as-is")),
    TRACKED_CHANGES["as-is"]);
  assert.equal(parseTrackedParam(url("trackedChanges=final")),
    TRACKED_CHANGES.final);
  assert.equal(parseTrackedParam(url("trackedChanges=original")),
    TRACKED_CHANGES.original);
  assert.equal(parseTrackedParam(url("trackedChanges=markup")),
    TRACKED_CHANGES.markup);
  assert.equal(parseTrackedParam(url("trackedChanges=show-markup")),
    TRACKED_CHANGES.markup);
  assert.equal(parseTrackedParam(url("")), null);
  assert.throws(
    () => parseTrackedParam(url("trackedChanges=bogus")),
    /unknown trackedChanges/);
});

test("parseRenderQuery collects every StreamOptions knob", () => {
  const q = parseRenderQuery(url(
    "parts=PAGES&dpi=72&firstPage=2&lastPage=3&format=svg&quality=60" +
    "&maxWidth=200&grayscale=1&timeout=30&trackedChanges=final" +
    "&skipHidden=1&usedRange=true&notes=yes"), PARTS);
  assert.deepEqual(q.parts, ["DOCUMENT_PART_PAGES"]);
  assert.equal(q.dpi, 72);
  assert.equal(q.firstPage, 2);
  assert.equal(q.lastPage, 3);
  assert.equal(q.format, "PAGE_IMAGE_FORMAT_SVG");
  assert.equal(q.quality, 60);
  assert.equal(q.maxWidth, 200);
  assert.equal(q.grayscale, true);
  assert.equal(q.timeout, 30);
  assert.equal(q.trackedChanges, "TRACKED_CHANGE_DISPLAY_FINAL");
  assert.equal(q.skipHidden, true);
  assert.equal(q.usedRange, true);
  assert.equal(q.notes, true);
});

test("buildStreamOptions maps query fields onto proto-loader names", () => {
  assert.equal(buildStreamOptions({}), null);
  const options = buildStreamOptions({
    parts: ["DOCUMENT_PART_PAGES"],
    dpi: 72,
    firstPage: 1,
    lastPage: 2,
    format: "PAGE_IMAGE_FORMAT_SVG",
    quality: 80,
    maxWidth: 200,
    grayscale: true,
    timeout: 15,
    trackedChanges: "TRACKED_CHANGE_DISPLAY_FINAL",
    skipHidden: true,
    usedRange: true,
    notes: true,
  });
  assert.deepEqual(options.parts, ["DOCUMENT_PART_PAGES"]);
  assert.equal(options.renderDpi, 72);
  assert.equal(options.firstPage, 1);
  assert.equal(options.lastPage, 2);
  assert.equal(options.pageFormat, "PAGE_IMAGE_FORMAT_SVG");
  assert.equal(options.vectorFormat, "PAGE_VECTOR_FORMAT_SVG");
  assert.equal(options.pageQuality, 80);
  assert.equal(options.maxWidthPx, 200);
  assert.equal(options.grayscale, true);
  assert.equal(options.timeoutSeconds, 15);
  assert.equal(options.trackedChanges, "TRACKED_CHANGE_DISPLAY_FINAL");
  assert.equal(options.skipHidden, true);
  assert.equal(options.paintUsedRange, true);
  assert.equal(options.includeNotesPages, true);
});

test("buildStreamOptions does not set vectorFormat for raster formats", () => {
  const jpeg = buildStreamOptions({ format: "PAGE_IMAGE_FORMAT_JPEG" });
  assert.equal(jpeg.pageFormat, "PAGE_IMAGE_FORMAT_JPEG");
  assert.equal(jpeg.vectorFormat, undefined);
  assert.equal(buildStreamOptions({ grayscale: false }), null);
});

test("buildPdfExtras puts range/timeout/tracked/skipHidden on the request", () => {
  assert.equal(buildPdfExtras({}), null);
  const extra = buildPdfExtras({
    firstPage: 2,
    lastPage: 2,
    timeout: 20,
    trackedChanges: "TRACKED_CHANGE_DISPLAY_ORIGINAL",
    skipHidden: true,
    grayscale: true, // not a PDF extra
  });
  assert.equal(extra.firstPage, 2);
  assert.equal(extra.lastPage, 2);
  assert.equal(extra.timeoutSeconds, 20);
  assert.equal(extra.trackedChanges, "TRACKED_CHANGE_DISPLAY_ORIGINAL");
  assert.equal(extra.skipHidden, true);
  assert.equal(extra.grayscale, undefined);
});

test("b64Size accounts for padding", () => {
  assert.equal(b64Size(""), 0);
  assert.equal(b64Size(null), 0);
  // "Man" -> TWFu (no pad), "Ma" -> TWE= (1 pad), "M" -> TQ== (2 pad)
  assert.equal(b64Size("TWFu"), 3);
  assert.equal(b64Size("TWE="), 2);
  assert.equal(b64Size("TQ=="), 1);
});

test("slimEvent keeps page images and strips typed-content bytes", () => {
  const page = { png: "TWFu" };
  assert.equal(slimEvent("pageImage", page), 3);
  assert.equal(page.png, "TWFu");

  const image = { data: "TWFu" };
  assert.equal(slimEvent("embeddedImage", image), 3);
  assert.equal(image.data, undefined);
  assert.equal(image.dataBytes, 3);

  const obj = { replacementImage: "TWE=" };
  assert.equal(slimEvent("embeddedObject", obj), 2);
  assert.equal(obj.replacementImage, undefined);
  assert.equal(obj.replacementImageBytes, 2);

  assert.equal(slimEvent("paragraph", { text: "hi" }), 0);
});
